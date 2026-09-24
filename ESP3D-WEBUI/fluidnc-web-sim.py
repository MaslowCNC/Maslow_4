#!/usr/bin/env python3

# Usage: ./fluidnc-web-sim.py [ FluidNC_IP_address ]
# Then browse to localhost:8080

# If the FluidNC_IP_address is omitted, fluidnc-web-sim will try to
# get the FluidNC IP address by resolving the address fluidnc.local

# This is a simple web server that serves index.html from a local file,
# and forwards other requests to a FluidNC system.
# It lets you test new WebUI builds without uploading index.html.gz to
# the FluidNC MCU.

# You might have to install some Python packages first, like zeroconf and flask
# For example "pip install flask zeroconf"

# It serves index.html directly from dist/index.html , which is where the
# WebUI build process puts that file.
#
# There is an alternate mode where this program can do some things directly,
# without needing a running FluidNC instance.  To enable the alternate mode,
# set the 'proxy' variable below to False.
#
# When proxy is False, this program serves file from the "test_files/" directory.
# Its subdirectories 'localfs/' and 'sd/' contain files that are presented as
# though they were in the FluidNC FLASH and SD card filesystems.
#
# The non-proxy-mode emulation is incomplete.  In particular, the following things
# are unimplemented:
#  - File upload
#  - OTA
#  - changing settings
#  - Running GCode - or anything that involves making FluidNC do any motion things
# But you can load the UI into your browser and do file downloads and whatnot

proxy = False

import asyncio
import os
import subprocess
import sys
import time
import websockets
import json
import warnings
import shutil
import requests
from zeroconf import Zeroconf, ServiceBrowser, ServiceStateChange

try:
  from flask import Flask, request, Response, send_from_directory
except:
  print("flash module missing; try 'pip3 install flask'")
  sys.exit(1)

def resolve_mdns(hostname):
    resolved_addresses = []

    class MyListener:
        def update_service(self, zeroconf, type, name):
            pass

        def remove_service(self, zeroconf, type, name):
            pass

        def add_service(self, zeroconf, type, name):
            info = zeroconf.get_service_info(type, name)
            if info:
                resolved_addresses.append(info.parsed_addresses()[0])

    zeroconf = Zeroconf()
    listener = MyListener()
    browser = ServiceBrowser(zeroconf, '_http._tcp.local.', listener)

    try:
        zeroconf.get_service_info('_http._tcp.local.', hostname + '._http._tcp.local.')
    finally:
        zeroconf.close()

    return resolved_addresses

fluidnc_ip = ''
def set_fluidnc_ip():
    global fluidnc_ip
    if 'FLUID_IP' in os.environ:
        fluidnc_ip = os.environ['FLUID_IP']
        print("Proxying to FluidNC at", fluidnc_ip)
    if len(sys.argv) == 2:
        fluidnc_ip = sys.argv[1]
        print(            "Proxying to FluidNC at", fluidnc_ip)
        return
    addresses = resolve_mdns('fluidnc.local')
    if len(addresses) == 0:
        print('fluidnc.local does not resolve; specify the FluidNC IP address on the command line')
        sys.exit(1)
    if len(addresses) == 1:
        fluidnc_ip = addresses[0]
        print("Proxying to FluidNC at", fluidnc_ip)
    else:
        print('fluidnc.local resolves to multiple addresses; specify the FluidNC IP address on the command line')
        sys.exit(1)

if proxy:
    set_fluidnc_ip()

def fluidnc_url():
    return 'http://' + fluidnc_ip + '/'

def fluidnc_websocket():
    if proxy:
        return '81:' + fluidnc_ip
    return '8081:localhost'

# 8081:localhost
python_path = sys.executable
script_path = os.path.realpath(__file__)
domain = 'localhost'
http_port = os.environ.get('PORT') or '8080'
ws_port = os.environ.get('WSPORT') or '8081'

### Flask Stuff
app = Flask(__name__)

test_files = 'test_files'

def normalize_relative_path(path):
    path = (path or '').replace('\\', '/')
    path = path.lstrip('/')
    normalized = os.path.normpath(path)
    if normalized in ('', '.'):
        return ''
    if normalized.startswith('..') or os.path.isabs(normalized):
        raise ValueError('Invalid path')
    return normalized.replace('\\', '/')

def fs_directory(fs, subdir=''):
    relative = normalize_relative_path(subdir)
    directory = os.path.join(test_files, fs, relative)
    os.makedirs(directory, exist_ok=True)
    return directory

def fs_file_path(fs, filepath=''):
    relative = normalize_relative_path(filepath)
    fullpath = os.path.join(test_files, fs, relative)
    parent = os.path.dirname(fullpath)
    if parent:
        os.makedirs(parent, exist_ok=True)
    return fullpath

def file_entry(directory, filename):
    fullpath = os.path.join(directory, filename)
    if os.path.isfile(fullpath):
        size = os.path.getsize(fullpath)
    else:
        size = -1
    return {
        'name': filename,
        'shortname': filename,
        'size': size,
        'datetime':''
    }

def format_bytes(n):
    if n < 1024:
        return str(n) + ' B'
    n = n / 1024
    if n < 1024:
        return str(round(n,2)) + ' KB'
    n = n / 1024
    if n < 1024:
        return str(round(n,2)) + ' MB'
    n = n / 1024
    if n < 1024:
        return str(round(n,2)) + ' GB'
    n = n / 1024
    return str(round(n,2)) + ' TB'

def make_files_list(fs, subdir, status):
    print("mfl", fs, subdir)
    directory = fs_directory(fs, subdir)
    print("dir", directory)
    diskusage = shutil.disk_usage(directory)
    disktotalsize = diskusage.total
    diskusedsize = diskusage.used
    files = {
        'files': [],
        'path': '',
        'total': format_bytes(disktotalsize),
        'used': '',
        'occupation': 0,
        'status': status
    }
    dirusedsize = 0
    for filename in os.listdir(directory):
        entry = file_entry(directory, filename)
        size = entry['size']
        files['files'].append(entry)
        if size != -1:
             dirusedsize = dirusedsize + size
    occupation = 100 * diskusedsize / disktotalsize
    files['used'] = format_bytes(diskusedsize)
    files['occupation'] = int(round(occupation, 0))
    return json.dumps(files)

sdfiles = '{"files":[{"name":"localfs","shortname":"localfs","size":"-1","datetime":""},{"name":"t2.nc","shortname":"t2.nc","size":"597","datetime":""},{"name":"SchluesselAutoX.nc","shortname":"SchluesselAutoX.nc","size":"61954","datetime":""}],"path":"","total":"14.83 GB","used":"384.00 KB","occupation":"0","status":"Ok"}'

gresp = "[GC:G1 G54 G17 G21 G90 G94 M5 M9 T0 F1000 S0]"

esp800resp = 'FW version: FluidNC v3.6.7 (Devt-5692a7c1-dirty) # FW target:grbl-embedded  # FW HW:Direct SD  # primary sd:/sd # secondary sd:none  # authentication:no # webcommunication: Sync: ' + fluidnc_websocket() + ' # hostname:fluidnc # axis:3'

esp400resp = '''
{"EEPROM":[
    {"F":"nvs",
      "P":"Firmware/Build",
      "H":"Firmware/Build",
      "T":"S",
      "V":"",
      "S":"20",
      "M":"0"
    },
    {"F":"tree",
      "P":"/board",
      "H":"/board",
      "T":"S",
      "V":"None",
      "S":"255",
      "M":"0"
    }
]
}
'''

connection_list = []
CONNECTIONS =  set()


def obsolete():
    plainval = request.args.get('plain')

def do_proxy(request):
    print("url1", request.url);
    # Forward the request to the desired endpoint
    url = request.url.replace(request.host_url, fluidnc_url())
    print("url2", url);
    try:
        response = requests.request(
            method=request.method,
            url=url,
            headers=request.headers,
            data=request.get_data(),
            cookies=request.cookies,
            stream=True
        )

        # Create a Flask response with the remote server's response data
        flask_response = response.raw

        # Set response headers
        headers = [(name, value) for name, value in response.headers.items()]
        flask_response.headers.extend(headers)

        return flask_response
    except requests.exceptions.RequestException as e:
        # Handle any errors that occur during the request
        error_message = {'error': str(e)}
        return error_message, 500

@app.route('/command')
def do_command():
    plainval = request.args.get('plain')

    # Respond directly to ESP800 instead of proxying it to FluidNC, because
    # we want to provide the correct websocket address
    if plainval == '[ESP800]':
        return esp800resp

    if proxy:
        return do_proxy(request)

    # If not proxying, respond to a few commands
    if plainval != None:
        if plainval == '[ESP400]':
            return esp400resp
        commandtextval = request.args.get('commandText')
        if commandtextval != None:
            print("commandText:", commandtextval)
            if commandtextval == '$G':
                if len(CONNECTIONS):
                    wsock = CONNECTIONS[0]
                    wsock.send(gresp)
    return ""

def handle_files(fs, request):
    if proxy:
        return do_proxy(request)
    method = request.method
    action = request.values.get('action')
    filename = request.values.get('filename')
    path = request.values.get('path')
    print("handle_files", method, action, filename, path)
    if path == None:
        path = ''
    print("handle_files", action, filename, path)

    if filename == None or filename == 'all':
        filename = ''

    path = normalize_relative_path(path)
    filename = normalize_relative_path(filename)
    if path == '':
        filepath = filename
    elif filename == '':
        filepath = path
    else:
        filepath = normalize_relative_path(path + '/' + filename)

    localpath = fs_file_path(fs, filepath) if filepath else fs_directory(fs, path)
    if method == 'POST':
        status = 'Ok'
        try:
            uploads = request.files.getlist('myfile[]')
            if not uploads:
                status = 'No file uploaded'
            for upload in uploads:
                uploaded_name = normalize_relative_path(upload.filename)
                save_path = fs_file_path(fs, uploaded_name or filepath)
                upload.save(save_path)
        except Exception as err:
            status = f'Upload failed: {err}'
    else:
        status = 'Ok'
        if action == 'delete':
            print("Deleting", filepath)
            try:
                os.remove(localpath)
            except:
                status = 'Cannot delete ' + filepath
        elif action == 'deletedir':
            try:
                shutil.rmtree(localpath)
            except:
                status = 'Cannot delete ' + filepath
        elif action == 'createdir':
            try:
                os.mkdir(localpath)
            except:
                status = 'Cannot create ' + filepath

    if request.args.get('dontlist') == None:
        return make_files_list(fs, path, status)
    return ""

@app.route('/upload', methods=['GET', 'POST'])
def upload():
    return handle_files('sd', request)

@app.route('/files', methods=['GET', 'POST'])
def do_files():
        return handle_files('localfs', request)

@app.route('/<path:filename>', methods=['GET'])
def do_get_file(filename):
    if proxy:
        return do_proxy(request)
    print("/ ", filename)
    if filename.startswith('SD/'):
        filename = filename[3:]
        return send_from_directory(fs_directory('sd'), filename)
    return send_from_directory(fs_directory('localfs'), filename)

@app.route('/', methods=['GET'])
def index():
    return send_from_directory('dist', 'index.html')

### Websocket stuff

async def register(websocket):
    CONNECTIONS.add(websocket)

async def unregister(websocket):
    CONNECTIONS.remove(websocket)

async def notify_users(message, websocket):
    global connection_list
    for connection in CONNECTIONS:
        if connection != websocket:
            connection_list.append(connection)

    await asyncio.wait([
        connection.send(message) for connection in connection_list
    ])

async def message_control(websocket, path):
    await register(websocket)
    try:
        await websocket.send("Connected")
        async for message in websocket:
            await notify_users(message, websocket)
    finally:
        await unregister(websocket)


if __name__ == '__main__':
    if not proxy:
        if len(sys.argv) == 2:
            if sys.argv[1] == 'run_socket':
                start_server = websockets.serve(message_control, domain, ws_port, subprotocols=['arduino'])
                asyncio.get_event_loop().run_until_complete(start_server)
                asyncio.get_event_loop().run_forever()
            else:
                time.sleep(1)
                print("Triggering websocket server")
                return_value = (subprocess.Popen([
                    python_path,
                    script_path,
                    'run_socket'
                ]))

    print("Starting flask server")
    app.run(port=http_port)
