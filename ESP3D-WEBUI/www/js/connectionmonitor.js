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
        
        // Check for timeouts periodically
        setInterval(() => this.checkTimeouts(), 500);
        
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
                message: 'Connection monitoring disabled',
                color: '#888'
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
                message: 'Initializing connection monitor...',
                color: '#ffa500'
            };
        }
        
        // Calculate packet loss percentage (of completed pings)
        const totalCompleted = totalReceived + totalLost;
        const lossPercent = totalCompleted > 0 ? (totalLost / totalCompleted) * 100 : 0;
        
        // Determine status
        let status, message, color;
        
        // Check for multiple tabs
        if (totalForeign > 3) {
            status = 'warning';
            message = `⚠️ Multiple tabs detected (${totalForeign} foreign pings)`;
            color = '#ff8c00';
        }
        // Good connection
        else if (lossPercent < 5 && avgLatency < 500) {
            status = 'good';
            message = `✓ Connected (${avgLatency}ms, ${lossPercent.toFixed(1)}% loss)`;
            color = '#4aa85c';
        }
        // Degraded connection
        else if (lossPercent < 20 || avgLatency < 1000) {
            status = 'degraded';
            message = `⚠️ Degraded (${avgLatency}ms, ${lossPercent.toFixed(1)}% loss)`;
            color = '#ffa500';
        }
        // Poor connection
        else if (lossPercent < 50) {
            status = 'poor';
            message = `⚠️ Poor connection (${lossPercent.toFixed(1)}% loss)`;
            color = '#ff6600';
        }
        // Connection lost
        else {
            status = 'lost';
            message = `✗ Connection lost (${lossPercent.toFixed(1)}% loss)`;
            color = '#ce654c';
        }
        
        return { status, message, color, lossPercent, avgLatency, totalForeign };
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
            indicator.innerHTML = statusInfo.message;
            indicator.style.backgroundColor = statusInfo.color;
            indicator.style.color = 'white';
        }
    }
};

// Auto-initialize when loaded
if (typeof document !== 'undefined') {
    document.addEventListener('DOMContentLoaded', () => {
        connectionMonitor.init();
    });
}
