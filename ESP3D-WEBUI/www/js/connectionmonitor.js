/*
 * connectionmonitor.js
 * Active connection monitoring system for bidirectional communication validation
 * 
 * Sends random numbers to firmware every 250ms and monitors responses to:
 * - Detect connection loss
 * - Measure packet loss
 * - Calculate round-trip latency
 * - Detect multiple tabs (receiving numbers not sent by this tab)
 */

let connectionMonitor = {
    // Configuration
    pingInterval: 250,  // ms between pings
    maxPingHistory: 50,  // keep last N pings for statistics
    timeoutThreshold: 2000,  // ms before considering a ping lost
    
    // State
    enabled: false,
    intervalId: null,
    timeoutIntervalId: null,  // Separate interval for timeout checks
    sentPings: new Map(),  // Map<number, timestamp>
    recentPingResults: [],  // Array of recent ping results {received: bool, lost: bool} - last 20
    maxRecentPings: 20,  // Track only last 20 pings for current status
    statistics: {
        totalSent: 0,
        totalReceived: 0,
        totalLost: 0,
        averageLatency: 0,
        latencies: []
    },
    wifiInfo: {
        ssid: 'N/A',
        signal: 'N/A',
        ip: 'N/A',
        mac: 'N/A',
        channel: 'N/A'
    },
    
    // UI state
    lastStatus: null,
    hovering: false,  // Track if mouse is hovering
    hoverUpdateInterval: null,  // Interval for updating tooltip while hovering
    
    /**
     * Initialize the connection monitor
     */
    init() {
        console.log("Connection Monitor: Initializing");
        this.hookIntoSocket();
    },
    
    /**
     * Start monitoring the connection
     */
    start() {
        if (this.enabled) {
            return;
        }
        
        console.log("Connection Monitor: Starting");
        this.enabled = true;
        this.statistics = {
            totalSent: 0,
            totalReceived: 0,
            totalLost: 0,
            averageLatency: 0,
            latencies: []
        };
        this.sentPings.clear();
        this.recentPingResults = [];  // Clear recent results
        
        // Start sending pings
        this.intervalId = setInterval(() => this.sendPing(), this.pingInterval);
        
        // Check for timeouts periodically (store separate ID to clear on stop)
        this.timeoutIntervalId = setInterval(() => this.checkTimeouts(), 500);
        
        this.updateUI();
    },
    
    /**
     * Stop monitoring the connection
     */
    stop() {
        if (!this.enabled) {
            return;
        }
        
        console.log("Connection Monitor: Stopping");
        this.enabled = false;
        
        if (this.intervalId) {
            clearInterval(this.intervalId);
            this.intervalId = null;
        }
        
        if (this.timeoutIntervalId) {
            clearInterval(this.timeoutIntervalId);
            this.timeoutIntervalId = null;
        }
        
        this.updateUI();
    },
    
    /**
     * Send a ping with a random number
     */
    sendPing() {
        if (!this.enabled) {
            return;
        }
        
        // Generate random number (1-999999)
        const pingNum = Math.floor(Math.random() * 999999) + 1;
        const now = Date.now();
        
        // Store the ping
        this.sentPings.set(pingNum, now);
        this.statistics.totalSent++;
        
        // Clean up old pings from the map
        const cutoff = now - this.timeoutThreshold * 3;
        for (const [num, timestamp] of this.sentPings.entries()) {
            if (timestamp < cutoff) {
                this.sentPings.delete(num);
            }
        }
        
        // Send via WebSocket directly (not HTTP) to avoid blocking during GCode execution
        const cmd = `ECHO:${pingNum}\n`;
        try {
            if (typeof ws_source !== 'undefined' && ws_source && ws_source.readyState === WebSocket.OPEN) {
                ws_source.send(cmd);
            }
        } catch (e) {
            console.log("Connection Monitor: Error sending ping: " + e);
        }
    },
    
    /**
     * Check for timed-out pings
     */
    checkTimeouts() {
        if (!this.enabled) {
            return;
        }
        
        const now = Date.now();
        let lostCount = 0;
        
        for (const [num, timestamp] of this.sentPings.entries()) {
            if (now - timestamp > this.timeoutThreshold) {
                lostCount++;
                this.sentPings.delete(num);
                this.statistics.totalLost++;
                
                // Track in recent results
                this.recentPingResults.push({ received: false, lost: true });
                if (this.recentPingResults.length > this.maxRecentPings) {
                    this.recentPingResults.shift();
                }
            }
        }
        
        if (lostCount > 0) {
            this.updateUI();
        }
    },
    
    /**
     * Handle received ECHO response
     */
    handleEcho(line) {
        if (!this.enabled) {
            return;
        }
        
        // Parse the number from "ECHO:12345"
        if (!line.startsWith("ECHO:")) {
            return;
        }
        
        const numStr = line.substring(5).trim();
        const pingNum = parseInt(numStr, 10);
        
        if (isNaN(pingNum)) {
            return;
        }
        
        // Check if we sent this ping
        if (this.sentPings.has(pingNum)) {
            const sentTime = this.sentPings.get(pingNum);
            const now = Date.now();
            const latency = now - sentTime;
            
            // Update statistics
            this.statistics.totalReceived++;
            this.statistics.latencies.push(latency);
            
            // Track in recent results
            this.recentPingResults.push({ received: true, lost: false });
            if (this.recentPingResults.length > this.maxRecentPings) {
                this.recentPingResults.shift();
            }
            
            // Keep only recent latencies
            if (this.statistics.latencies.length > this.maxPingHistory) {
                this.statistics.latencies.shift();
            }
            
            // Calculate average
            if (this.statistics.latencies.length > 0) {
                const sum = this.statistics.latencies.reduce((a, b) => a + b, 0);
                this.statistics.averageLatency = Math.round(sum / this.statistics.latencies.length);
            }
            
            // Remove from pending
            this.sentPings.delete(pingNum);
            
            this.updateUI();
        }
        // Ignore pings that we didn't send (could be from stale data or other sources)
    },
    
    /**
     * Hook into the socket message handler to intercept ECHO responses
     */
    hookIntoSocket() {
        // We need to intercept WebSocket messages
        // This will be called from socket.js when messages are received
        console.log("Connection Monitor: Hooked into socket");
    },
    
    /**
     * Fetch WiFi information from the ESP
     */
    async fetchWifiInfo() {
        try {
            // Use ESP420 command to get WiFi status
            const cmd = buildHttpCommandCmd(httpCmdType.plain, "[ESP420]plain");
            const response = await new Promise((resolve, reject) => {
                SendGetHttp(cmd, resolve, reject);
            });
            
            // Parse the response for WiFi information
            // Response format varies, but typically includes SSID, Signal, IP, MAC, Channel
            const lines = response.split('\n');
            for (const line of lines) {
                if (line.includes('SSID:')) {
                    this.wifiInfo.ssid = line.split(':')[1]?.trim() || 'N/A';
                } else if (line.includes('Signal:')) {
                    this.wifiInfo.signal = line.split(':')[1]?.trim() || 'N/A';
                } else if (line.includes('IP:')) {
                    this.wifiInfo.ip = line.split(':')[1]?.trim() || 'N/A';
                } else if (line.includes('MAC:')) {
                    this.wifiInfo.mac = line.split(':')[1]?.trim() || 'N/A';
                } else if (line.includes('Channel:')) {
                    this.wifiInfo.channel = line.split(':')[1]?.trim() || 'N/A';
                }
            }
        } catch (e) {
            console.log("Connection Monitor: Error fetching WiFi info: " + e);
        }
    },
    
    /**
     * Get current connection status
     */
    getStatus() {
        if (!this.enabled) {
            return {
                status: 'disabled',
                displayText: 'Connection Monitoring',
                tooltip: 'Connection monitoring is disabled',
                color: '#888',
                showWarning: false
            };
        }
        
        const totalSent = this.statistics.totalSent;
        const totalReceived = this.statistics.totalReceived;
        const totalLost = this.statistics.totalLost;
        const avgLatency = this.statistics.averageLatency;
        
        // Not enough data yet
        if (totalSent < 5) {
            return {
                status: 'starting',
                displayText: 'Wifi<br>Connection',
                tooltip: 'Initializing connection monitor...',
                color: '#ffa500',
                showWarning: false
            };
        }
        
        // Calculate packet loss percentage based on RECENT pings (last 20)
        let recentReceived = 0;
        let recentLost = 0;
        for (const result of this.recentPingResults) {
            if (result.received) recentReceived++;
            if (result.lost) recentLost++;
        }
        const recentTotal = recentReceived + recentLost;
        const lossPercent = recentTotal > 0 ? (recentLost / recentTotal) * 100 : 0;
        
        // Check for severely degraded connection (many pending pings AND high recent loss)
        const now = Date.now();
        let veryOldPendingCount = 0;  // Count pings older than 4 seconds
        for (const [num, timestamp] of this.sentPings.entries()) {
            const age = now - timestamp;
            if (age > this.timeoutThreshold * 2) {  // 4 seconds
                veryOldPendingCount++;
            }
        }
        
        // Connection is truly lost only if multiple pings are VERY old AND recent loss is high
        const connectionTimedOut = veryOldPendingCount >= 3 && lossPercent > 80;
        
        // WiFi info section for tooltip
        const wifiSection = `\n\n━━━ WiFi Information ━━━\nSSID: ${this.wifiInfo.ssid}\nSignal Strength: ${this.wifiInfo.signal}\nIP Address: ${this.wifiInfo.ip}\nMAC Address: ${this.wifiInfo.mac}\nChannel: ${this.wifiInfo.channel}`;
        
        // Determine status
        let status, tooltip, color, showWarning;
        const displayText = 'Wifi<br>Connection';
        
        // Connection timed out - no responses for extended period
        if (connectionTimedOut) {
            status = 'lost';
            tooltip = `Connection Status: LOST\nRecent Packet Loss: ${lossPercent.toFixed(1)}% (last ${recentTotal} pings)\nPings Sent: ${totalSent}\nPings Received: ${totalReceived}\n\nMachine may be powered off or disconnected!${wifiSection}`;
            color = '#ce654c';
            showWarning = true;
        }
        // Good connection
        else if (lossPercent < 5 && avgLatency < 500) {
            status = 'good';
            tooltip = `Connection Status: Good\nLatency: ${avgLatency}ms\nRecent Packet Loss: ${lossPercent.toFixed(1)}% (last ${recentTotal} pings)\nTotal Pings Sent: ${totalSent}\nTotal Received: ${totalReceived}${wifiSection}`;
            color = '#4aa85c';
            showWarning = false;
        }
        // Degraded connection
        else if (lossPercent < 20 || avgLatency < 1000) {
            status = 'degraded';
            tooltip = `Connection Status: Degraded\nLatency: ${avgLatency}ms\nRecent Packet Loss: ${lossPercent.toFixed(1)}% (last ${recentTotal} pings)\nRecent Received: ${recentReceived}\nRecent Lost: ${recentLost}${wifiSection}`;
            color = '#ffa500';
            showWarning = true;
        }
        // Poor connection
        else if (lossPercent < 50) {
            status = 'poor';
            tooltip = `Connection Status: Poor\nRecent Packet Loss: ${lossPercent.toFixed(1)}% (last ${recentTotal} pings)\nRecent Received: ${recentReceived}\nRecent Lost: ${recentLost}\n\nConsider checking your WiFi connection.${wifiSection}`;
            color = '#ff6600';
            showWarning = true;
        }
        // Connection lost (high packet loss)
        else {
            status = 'lost';
            tooltip = `Connection Status: LOST\nRecent Packet Loss: ${lossPercent.toFixed(1)}% (last ${recentTotal} pings)\nRecent Received: ${recentReceived}\nRecent Lost: ${recentLost}\n\nConnection to machine may be lost!${wifiSection}`;
            color = '#ce654c';
            showWarning = true;
        }
        
        return { status, displayText, tooltip, color, showWarning, lossPercent, avgLatency };
    },
    
    /**
     * Update the UI with current status
     */
    updateUI() {
        const statusInfo = this.getStatus();
        
        // Always update tooltip if hovering (for real-time updates)
        // Update display if status changed
        const statusChanged = JSON.stringify(statusInfo) !== JSON.stringify(this.lastStatus);
        
        if (!statusChanged && !this.hovering) {
            return;
        }
        
        this.lastStatus = statusInfo;
        
        const indicator = document.getElementById('connection-status-indicator');
        if (indicator) {
            // Set display text with optional warning icon
            const warningIcon = statusInfo.showWarning ? ' !' : '';
            indicator.innerHTML = statusInfo.displayText + warningIcon;
            
            // Set background color
            indicator.style.backgroundColor = statusInfo.color;
            indicator.style.color = 'white';
            
            // Update custom tooltip content if it exists
            const customTooltip = document.getElementById('connection-monitor-tooltip');
            if (customTooltip && this.hovering) {
                // Replace \n with <br> for HTML display
                customTooltip.innerHTML = statusInfo.tooltip.replace(/\n/g, '<br>');
            }
            
            // Setup hover listeners and custom tooltip if not already done
            if (!indicator.hasAttribute('data-hover-setup')) {
                indicator.setAttribute('data-hover-setup', 'true');
                
                // Create custom tooltip element
                const tooltip = document.createElement('div');
                tooltip.id = 'connection-monitor-tooltip';
                tooltip.style.cssText = `
                    position: fixed;
                    background-color: rgba(0, 0, 0, 0.9);
                    color: white;
                    padding: 8px 12px;
                    border-radius: 4px;
                    font-size: 12px;
                    font-family: monospace;
                    white-space: pre-line;
                    z-index: 10000;
                    pointer-events: none;
                    display: none;
                    max-width: 400px;
                    line-height: 1.4;
                `;
                document.body.appendChild(tooltip);
                
                indicator.addEventListener('mouseenter', (e) => {
                    this.hovering = true;
                    // Fetch WiFi info when user hovers
                    this.fetchWifiInfo();
                    // Show custom tooltip
                    tooltip.style.display = 'block';
                    tooltip.innerHTML = statusInfo.tooltip.replace(/\n/g, '<br>');
                    // Start interval to update tooltip while hovering
                    if (this.hoverUpdateInterval) {
                        clearInterval(this.hoverUpdateInterval);
                    }
                    this.hoverUpdateInterval = setInterval(() => {
                        if (this.hovering) {
                            this.updateUI();
                        }
                    }, 500);  // Update every 500ms while hovering
                });
                
                indicator.addEventListener('mousemove', (e) => {
                    // Position tooltip near mouse cursor
                    tooltip.style.left = (e.clientX + 15) + 'px';
                    tooltip.style.top = (e.clientY + 15) + 'px';
                });
                
                indicator.addEventListener('mouseleave', () => {
                    this.hovering = false;
                    // Hide custom tooltip
                    tooltip.style.display = 'none';
                    // Stop updating when not hovering
                    if (this.hoverUpdateInterval) {
                        clearInterval(this.hoverUpdateInterval);
                        this.hoverUpdateInterval = null;
                    }
                });
            }
        }
    }
};

// Auto-initialize when loaded
if (typeof document !== 'undefined') {
    document.addEventListener('DOMContentLoaded', () => {
        connectionMonitor.init();
    });
}
