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
    receivedPings: [],  // Array of received numbers
    statistics: {
        totalSent: 0,
        totalReceived: 0,
        totalLost: 0,
        totalForeign: 0,  // Numbers received that we didn't send
        averageLatency: 0,
        latencies: []
    },
    
    // UI state
    lastStatus: null,
    
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
            totalForeign: 0,
            averageLatency: 0,
            latencies: []
        };
        this.sentPings.clear();
        this.receivedPings = [];
        
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
        
        // Send via WebSocket
        const cmd = `ECHO:${pingNum}`;
        try {
            if (typeof SendPrinterCommand === 'function') {
                SendPrinterCommand(cmd, false, true);
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
        } else {
            // This is a foreign ping (from another tab or stale)
            this.statistics.totalForeign++;
        }
        
        this.receivedPings.push(pingNum);
        if (this.receivedPings.length > this.maxPingHistory) {
            this.receivedPings.shift();
        }
        
        this.updateUI();
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
        const totalForeign = this.statistics.totalForeign;
        const avgLatency = this.statistics.averageLatency;
        
        // Not enough data yet
        if (totalSent < 5) {
            return {
                status: 'starting',
                displayText: 'Connection Monitoring',
                tooltip: 'Initializing connection monitor...',
                color: '#ffa500',
                showWarning: false
            };
        }
        
        // Calculate packet loss percentage (of completed pings)
        // Note: This excludes pings still in flight (pending in sentPings Map)
        // to avoid showing artificially high loss during normal operation
        const totalCompleted = totalReceived + totalLost;
        const lossPercent = totalCompleted > 0 ? (totalLost / totalCompleted) * 100 : 0;
        
        // Determine status
        let status, tooltip, color, showWarning;
        const displayText = 'Connection Monitoring';
        
        // Check for multiple tabs
        if (totalForeign > 3) {
            status = 'warning';
            tooltip = `WARNING: Multiple tabs detected!\n${totalForeign} foreign pings received.\nOnly one tab should control the machine at a time.`;
            color = '#ff8c00';
            showWarning = true;
        }
        // Good connection
        else if (lossPercent < 5 && avgLatency < 500) {
            status = 'good';
            tooltip = `Connection Status: Good\nLatency: ${avgLatency}ms\nPacket Loss: ${lossPercent.toFixed(1)}%\nPings Sent: ${totalSent}\nPings Received: ${totalReceived}`;
            color = '#4aa85c';
            showWarning = false;
        }
        // Degraded connection
        else if (lossPercent < 20 || avgLatency < 1000) {
            status = 'degraded';
            tooltip = `Connection Status: Degraded\nLatency: ${avgLatency}ms\nPacket Loss: ${lossPercent.toFixed(1)}%\nPings Sent: ${totalSent}\nPings Received: ${totalReceived}\nPings Lost: ${totalLost}`;
            color = '#ffa500';
            showWarning = true;
        }
        // Poor connection
        else if (lossPercent < 50) {
            status = 'poor';
            tooltip = `Connection Status: Poor\nPacket Loss: ${lossPercent.toFixed(1)}%\nPings Sent: ${totalSent}\nPings Received: ${totalReceived}\nPings Lost: ${totalLost}\n\nConsider checking your WiFi connection.`;
            color = '#ff6600';
            showWarning = true;
        }
        // Connection lost
        else {
            status = 'lost';
            tooltip = `Connection Status: LOST\nPacket Loss: ${lossPercent.toFixed(1)}%\nPings Sent: ${totalSent}\nPings Received: ${totalReceived}\nPings Lost: ${totalLost}\n\nConnection to machine may be lost!`;
            color = '#ce654c';
            showWarning = true;
        }
        
        return { status, displayText, tooltip, color, showWarning, lossPercent, avgLatency, totalForeign };
    },
    
    /**
     * Update the UI with current status
     */
    updateUI() {
        const statusInfo = this.getStatus();
        
        // Only update if status changed
        if (JSON.stringify(statusInfo) === JSON.stringify(this.lastStatus)) {
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
            
            // Set tooltip with detailed information
            indicator.title = statusInfo.tooltip;
        }
    }
};

// Auto-initialize when loaded
if (typeof document !== 'undefined') {
    document.addEventListener('DOMContentLoaded', () => {
        connectionMonitor.init();
    });
}
