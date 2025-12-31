// Initialize variables
var lastTime;
var gauge;

// Get current sensor readings and send browser time when the page loads
window.addEventListener('load', function() {
  getReadings();
  sendBrowserTime();
  
  // Set up periodic time updates every 30 seconds
  setInterval(sendBrowserTime, 30000);
  
  // Get container and adjust gauge size - moved inside load event
  var container = document.getElementById('coop-card');
  var size = Math.min(container.offsetWidth * 0.6, 250);

  var opts = {
    fontSize: 35,
    angle: 0.15, // The span of the gauge arc
    lineWidth: 0.4,
    radiusScale: 0.65,
    renderTicks: {
      divisions: 5,
      divWidth: 1.1,
      divLength: 0.7,
      divColor: '#333333',
      subDivisions: 3,
      subLength: 0.5,
      subWidth: 0.6,
      subColor: '#666666'
    },
    pointer: {
      length: 0.6, // // Relative to gauge radius
      strokeWidth: 0.035, // The thickness
      color: '#000000' // Fill color
    },
    limitMax: false,     // If false, max value increases automatically if value > maxValue
    limitMin: false,     // If true, the min value of the gauge will be fixed
    colorStart: '#6FADC0',   // Colors
    colorStop: '#a2ede3ff',
    strokeColor: '#E0E0E0',
    generateGradient: true,
    highDpiSupport: true,     // High resolution support
  };

  // Set canvas size dynamically based on container with extra padding
  var target = document.getElementById('gauge-coop'); // your canvas element

  // Make sure we have a square canvas with plenty of room for the gauge
  var canvasSize = Math.max(size * 1.5, 250);  // Ensure minimum size of 250px
  target.width = canvasSize;
  target.height = canvasSize;

  // Adjust gauge options based on canvas size
  opts.width = canvasSize;
  opts.height = canvasSize;

  // Center the gauge within the canvas
  opts.renderTo = target;
  opts.centerX = canvasSize / 2;
  opts.centerY = canvasSize / 2;

  gauge = new Gauge(target).setOptions(opts); // create sexy gauge!
  gauge.maxValue = 100; // set max gauge value
  gauge.setMinValue(0);  // Prefer setter over gauge.minValue = 0
  gauge.animationSpeed = 32; // set animation speed (32 is default value)
  //gauge.set(1250); // set actual value
});

// Function to get current readings on the webpage when it loads for the first time
function getReadings(){
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var myObj = JSON.parse(this.responseText);
      console.log(myObj);
      
      // Only set gauge value if gauge has been initialized
      if (gauge) {
        gauge.set(myObj.loadcell);
      }
      
      // Display loadcell value
      document.getElementById('feed').textContent = myObj.loadcell;
      
      // Update units if available
      if (myObj.units) {
        document.getElementById('units').textContent = myObj.units;
      }
      
      // Update last refresh time if available
      if (myObj.lastUpdate) {
        updateLastRefreshDisplay(myObj.lastUpdate);
      }
    }
  };
  xhr.open("GET", "/readings", true);
  xhr.send();
}

// Function to format and display the last refresh time
function updateLastRefreshDisplay(timestamp) {
  const refreshElement = document.getElementById('last-refresh');
  if (refreshElement) {
    // Ensure timestamp is treated as a number
    const timestampNum = typeof timestamp === 'string' ? parseInt(timestamp, 10) : timestamp;
    
    // Check if the timestamp is a reasonable Unix timestamp (at least from year 2020)
    // Unix timestamp for Jan 1, 2020 is 1577836800000 milliseconds
    if (timestampNum < 1577836800000 && timestampNum > 0) {
      // If it's a small number, it's likely just milliseconds since ESP32 boot
      // Use current browser time instead
      refreshElement.textContent = new Date().toLocaleString();
      console.log("Small timestamp detected, using current browser time instead:", timestampNum);
    } else {
      // It's a proper Unix timestamp, use it
      const date = new Date(timestampNum);
      
      // Check if date is valid before displaying
      if (!isNaN(date.getTime())) {
        refreshElement.textContent = date.toLocaleString();
      } else {
        console.error("Invalid timestamp received:", timestamp);
        refreshElement.textContent = "Unknown";
      }
    }
  }
}

// Function to send the browser's local time to the server
function sendBrowserTime() {
  // Get current browser time
  const now = new Date();
  console.log(now.getTime(), now.toLocaleString(), now.toISOString());

  // Format the time data
  const timeData = {
    timestamp: now.getTime(), // This is milliseconds since epoch (Unix timestamp)
    timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
    offset: now.getTimezoneOffset(),
    localTime: now.toLocaleString(),
    isoString: now.toISOString()
  };
  
  console.log("Sending browser time to server:", timeData);
  
  // Send the time data to the server
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      console.log("Browser time sent to server");
    }
  };
  xhr.open("POST", "/browsertime", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.send(JSON.stringify(timeData));
  
  // Display local time on the page if there's a time element
  const timeElement = document.getElementById('local-time');
  if (timeElement) {
    timeElement.textContent = now.toLocaleString();
  }
}

if (!!window.EventSource) {
  var source = new EventSource('/events');
  
  source.addEventListener('open', function(e) {
    console.log("Events Connected");
  }, false);

  source.addEventListener('error', function(e) {
    if (e.target.readyState != EventSource.OPEN) {
      console.log("Events Disconnected");
    }
  }, false);
  
  source.addEventListener('message', function(e) {
    console.log("message", e.data);
  }, false);
  
  source.addEventListener('new_readings', function(e) {
    console.log("new_readings", e.data);
    var myObj = JSON.parse(e.data);
    console.log(myObj);
    var timeDelta = myObj.time - lastTime;
    lastTime = myObj.time;
    console.log(timeDelta/1000);
    
    // Only set gauge value if gauge has been initialized
    if (gauge) {
      gauge.set(myObj.loadcell);
    }
    
    // Display loadcell value
    document.getElementById('feed').textContent = myObj.loadcell;
    
    // Update units if available
    if (myObj.units) {
      document.getElementById('units').textContent = myObj.units;
    }
    
    // Update last refresh time if available
    if (myObj.lastUpdate) {
      updateLastRefreshDisplay(myObj.lastUpdate);
    }
  }, false);
}