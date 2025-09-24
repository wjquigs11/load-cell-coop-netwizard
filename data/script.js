// Get current sensor readings when the page loads  
window.addEventListener('load', getReadings);

// Get container and adjust gauge size 
var container = document.getElementById('coop-card');
var size = Math.min(container.offsetWidth * 0.6, 250); 
var lastTime;

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
  colorStart: '#6FADCF',   // Colors
  colorStop: '#8FC0DA',    // just experiment with them
  strokeColor: '#E0E0E0',  // to see which ones work best for you
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

var gauge = new Gauge(target).setOptions(opts); // create sexy gauge!
gauge.maxValue = 100; // set max gauge value
gauge.setMinValue(0);  // Prefer setter over gauge.minValue = 0
gauge.animationSpeed = 32; // set animation speed (32 is default value)
//gauge.set(1250); // set actual value

// Function to get current readings on the webpage when it loads for the first time
function getReadings(){
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var myObj = JSON.parse(this.responseText);
      console.log(myObj);
      gauge.set(myObj.loadcell);
    }
  }; 
  xhr.open("GET", "/readings", true);
  xhr.send();
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
    gauge.set(myObj.loadcell);
  }, false);
}