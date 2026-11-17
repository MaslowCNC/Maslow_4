#!/usr/bin/env node
// Dev server for the web UI: serves a fresh build of www/ and proxies everything
// else (HTTP and the WebSocket) to a real machine, so UI edits show up on a
// browser refresh without flashing.
//
//   npm run dev                      # machine at maslow.local, UI at http://localhost:8484
//   node dev-server.js 192.168.1.50 9000
//
// On each page load it runs `gulp package2` (unminified build, ~3 s) if anything
// under www/ changed since the last build. Tabs in one browser share a session, so the
// newest tab takes the machine connection (as on the machine itself).

const http = require('http')
const net = require('net')
const dns = require('dns')
const crypto = require('crypto')
const fs = require('fs')
const path = require('path')
const { execFile } = require('child_process')

const machineHost = process.argv[2] || 'maslow.local'
const port = Number(process.argv[3] || 8484)
const root = __dirname
const wwwDir = path.join(root, 'www')
const builtIndex = path.join(root, 'dist', 'index.html')
const gulpBin = path.join(root, 'node_modules', '.bin', 'gulp')

let machineIp = null // resolved once: mDNS lookups can be slow
let lastBuild = 0
let building = null

const newestMtime = (dir) => {
	let newest = 0
	for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, entry.name)
		const t = entry.isDirectory() ? newestMtime(p) : fs.statSync(p).mtimeMs
		if (t > newest) newest = t
	}
	return newest
}

const build = () => {
	if (building) return building
	const started = Date.now()
	building = new Promise((resolve, reject) => {
		execFile(gulpBin, ['package2'], { cwd: root }, (err, stdout, stderr) => {
			building = null
			if (err) {
				console.error(stdout, stderr)
				return reject(err)
			}
			lastBuild = started
			console.log(`built in ${Date.now() - started} ms`)
			resolve()
		})
	})
	return building
}

// The firmware ties each WebSocket to a sessionId cookie and closes older sockets of the same
// session. It sets that cookie when it serves index.html, which it doesn't do here, so give each
// browser its own; without one, every dev-server tab shares the empty session and they keep
// closing each other's sockets.
const sessionCookie = (req) => {
	if (/(?:^|;\s*)sessionId=/.test(req.headers.cookie || '')) return {}
	return { 'Set-Cookie': `sessionId=${crypto.randomBytes(8).toString('hex')}; Path=/` }
}

const serveIndex = async (req, res) => {
	try {
		if (!fs.existsSync(builtIndex) || newestMtime(wwwDir) > lastBuild) await build()
		res.writeHead(200, { 'Content-Type': 'text/html', 'Cache-Control': 'no-store', ...sessionCookie(req) })
		fs.createReadStream(builtIndex).pipe(res)
	} catch (err) {
		res.writeHead(500, { 'Content-Type': 'text/plain' })
		res.end(`Build failed:\n${err}`)
	}
}

// [ESP800] tells the page which port to open the WebSocket on ("Sync: 80").
// Point it at this server instead so the socket goes through the proxy too.
const rewriteBody = (body) => body.replace(/(webcommunication:\s*Sync:\s*)\d+/, `$1${port}`)

const proxy = (req, res) => {
	const headers = { ...req.headers, host: machineHost }
	delete headers['accept-encoding'] // keep /command bodies plain so we can rewrite them
	const upstream = http.request(
		{ host: machineIp, port: 80, method: req.method, path: req.url, headers },
		(up) => {
			if (!req.url.startsWith('/command')) {
				res.writeHead(up.statusCode, up.headers)
				return up.pipe(res)
			}
			const chunks = []
			up.on('data', (c) => chunks.push(c))
			up.on('end', () => {
				const body = Buffer.from(rewriteBody(Buffer.concat(chunks).toString('latin1')), 'latin1')
				const h = { ...up.headers, 'content-length': body.length }
				delete h['transfer-encoding']
				res.writeHead(up.statusCode, h)
				res.end(body)
			})
		}
	)
	upstream.on('error', (err) => {
		console.error(`${req.method} ${req.url}: ${err.code || ''} ${err.message}`)
		if (res.headersSent) return res.destroy()
		res.writeHead(502, { 'Content-Type': 'text/plain' })
		res.end(`Machine not reachable: ${err.message}`)
	})
	req.pipe(upstream)
}

const server = http.createServer((req, res) => {
	const p = req.url.split('?')[0]
	if (p === '/' || p === '/index.html') {
		if (req.method === 'GET') return serveIndex(req, res)
		if (req.method === 'HEAD') return res.writeHead(200).end() // health probes; the firmware sends a body on HEAD
	}
	proxy(req, res)
})

// WebSocket: replay the upgrade request to the machine and pipe raw bytes both ways.
server.on('upgrade', (req, socket, head) => {
	const up = net.connect(80, machineIp, () => {
		const lines = [`${req.method} ${req.url} HTTP/1.1`]
		for (let i = 0; i < req.rawHeaders.length; i += 2) {
			const name = req.rawHeaders[i]
			const value = name.toLowerCase() === 'host' ? machineHost : req.rawHeaders[i + 1]
			lines.push(`${name}: ${value}`)
		}
		up.write(lines.join('\r\n') + '\r\n\r\n')
		if (head && head.length) up.write(head)
		up.pipe(socket)
		socket.pipe(up)
	})
	const close = () => {
		up.destroy()
		socket.destroy()
	}
	up.on('error', close)
	socket.on('error', close)
	up.on('close', close)
	socket.on('close', close)
})

dns.lookup(machineHost, { family: 4 }, (err, address) => {
	if (err) {
		console.error(`Cannot resolve ${machineHost}: ${err.message}`)
		process.exit(1)
	}
	machineIp = address
	server.listen(port, () => {
		console.log(`UI dev server: http://localhost:${port}  ->  ${machineHost} (${machineIp})`)
	})
})
