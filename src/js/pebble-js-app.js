var geocalcs = {};
geocalcs.er = 6378.0; /* equatorial radius km */
geocalcs.pr = 6357.0; /* polar radius km */
geocalcs.e = Math.sqrt(1.0 - (geocalcs.pr * geocalcs.pr) / (geocalcs.er * geocalcs.er)); /* eccentricity */
geocalcs.e2 = geocalcs.e * geocalcs.e;
geocalcs.bearingLabels = [
  "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
  "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
];

geocalcs.deg2rad = function (loc) {
  return {lat: Math.PI * loc.lat / 180.0, lng: Math.PI * loc.lng / 180.0};
};

/* Calculate the great circle distance between two locations on a sphere.
   Distance returned in kilometres. */
geocalcs.gcDistance = function (loc1, loc2) {
  var loc1r = geocalcs.deg2rad(loc1);
  var loc2r = geocalcs.deg2rad(loc2);
  var t = Math.cos(loc1r.lat) * Math.cos(loc2r.lat) * Math.cos(loc2r.lng - loc1r.lng) +
          Math.sin(loc1r.lat) * Math.sin(loc2r.lat);
  if (t < -1) t = -1;
  if (t > 1) t = 1;
  return Math.acos(t);
};

/* Calculate the great circle distance between two locations on the Earth.
   Location lat/long in degrees. N/E is positive, S/W is negative.
   Distance returned in kilometers. */
geocalcs.earthDistance = function (loc1, loc2) {
  var loc1r = geocalcs.deg2rad(loc1);
  var loc2r = geocalcs.deg2rad(loc2);
  
  /* do ellipsoid corrections to radius */
  var radius = Math.sin((loc1r.lat + loc2r.lat) / 2.0);
  radius = 1.0 - geocalcs.e2 * radius * radius;
  radius = geocalcs.er * Math.sqrt(1.0 - geocalcs.e2) / radius;

  return geocalcs.gcDistance(loc1, loc2) * radius;
};

/* Calculate the bearing between two locations. */
geocalcs.bearing = function (loc1, loc2) {
  var loc1r = geocalcs.deg2rad(loc1);
  var loc2r = geocalcs.deg2rad(loc2);
  var c, d;
  if (Math.cos(loc1r.lat) < 0.000001) {
    if (loc1r.lat > 0.0) {
      c = Math.PI; /* starting from N pole */
    } else {
      c = 0.0; /* starting from S pole */
    }
  } else {
    d = geocalcs.gcDistance(loc1, loc2);
    c = (Math.sin(loc2r.lat) - Math.sin(loc1r.lat) * Math.cos(d)) /
        (Math.sin(d) * Math.cos(loc1r.lat));
    if (c < -1) c = -1;
    if (c > 1) c = 1;
    c = Math.acos(c);
    if (Math.sin(loc2r.lng - loc1r.lng) < 0.0) {
      c = Math.PI * 2.0 - c;
    }
  }
  return c * 180.0 / Math.PI;
};

/* Text compass bearing of numeric bearing. */
geocalcs.compassBearing = function (b) {
  var c = b + 11.25;
  if (c >= 360.0) c -= 360.0;
  c = geocalcs.bearingLabels[Math.floor(c / 22.5)];
  if (!c) c = '-';
  return c;
};

var app_msgs = [];

function send_app_msgs(e) {
  if (app_msgs.length > 0) {
    Pebble.sendAppMessage(app_msgs.shift(), send_app_msgs);
  }
}

function make_text_message(msg) {
  return {
    stop_number: 0,
    stop_location: msg,
    stop_name: ""
  };
}

function calc_stop_info(pos, stopinfo) {
  stopinfo.dst = Math.floor(geocalcs.earthDistance(pos, stopinfo) * 1000.0); // metres
  if (stopinfo.dst < 1000.0) {
    stopinfo.dst = '' + stopinfo.dst + 'm';
  } else {
    stopinfo.dst = '' + Math.round(stopinfo.dst / 100.0) / 10.0 + 'km';
  }
  // compass direction, eg "E"
  stopinfo.dst = stopinfo.dst + ' ' + geocalcs.compassBearing(geocalcs.bearing(pos, stopinfo));
  // save
  // stopinfo.identifier: "12345"
  // stopinfo.name: "Abbey Rd Before Beatle Ln"
  stopinfo.name = stopinfo.name.replace(" Before ", " b ");
  stopinfo.name = stopinfo.name.replace(" After ", " a ");
  return stopinfo;
}

function make_stop_message(stopinfo) {
  return {
    stop_number: Math.floor(stopinfo.stop_number),
    stop_location: stopinfo.dst.substr(0, 10),
    stop_name: stopinfo.name.substr(0, 30)
  };
}

function send_bus_stops_near(pos) {
  app_msgs = [];
  var req = new XMLHttpRequest();
  req.open('GET', 'http://api.perthtransit.com/1/bus_stops?near=' + pos.lat + ',' + pos.lng, true);
  req.timeout = 15000;
  req.onload = function(e) {
    if (req.readyState == 4) {
      if (((req.status == 200) || (req.status == 304)) && (req.responseText.length > 0)) {
        // request OK
        var rsp = JSON.parse(req.responseText).response;
        for (var idx in rsp) {
          app_msgs.push(make_stop_message(calc_stop_info(pos, rsp[idx])));
        }
        if (app_msgs.length === 0) {
          app_msgs.push(make_text_message("No stops nearby"));
        }
      } else {
        // request error
        app_msgs.push(make_text_message("No comms"));
      }
      send_app_msgs();
    }
  };
  req.send(null);
}

function position_ok(position) {
  Pebble.sendAppMessage(make_text_message("Finding stops"));
  send_bus_stops_near({
    lat: position.coords.latitude,
    lng: position.coords.longitude
  });
  //send_bus_stops_near({
  //  lat: -32.051655,
  //  lng: 115.746192
  //});
}

function position_fail(error) {
  Pebble.sendAppMessage(make_text_message("No location"));
}

Pebble.addEventListener("ready",
  function(e) {
    console.log("ready");
    setTimeout(function() {
      Pebble.sendAppMessage(make_text_message("Locating"));
      navigator.geolocation.getCurrentPosition(position_ok, position_fail,
        {enableHighAccuracy: true, timeout: 60 * 1000, maximumAge: 10 * 60 * 1000});
    }, 10);
  }
);

function make_service_message(service) {
  return {
    service_info: service.time.substr(0, 5) + ' ' + service.route.substr(0, 3),
    service_dest: service.destination.substr(0, 30)
  };
}

function bus_services_ok(response) {
  console.log("got bus routes");
  // response.lat = -12.3456
  // response.lng = 123.456
  // response.stop_number = "12345"
  // response.name = "Abbey Rd Before Beatle Ln"
  // response.identifier = "12345"
  for (var idx in response.times) {
    var service = response.times[idx];
    // service.time = "12:34"
    // service.route = "123"
    // service.destination = "To Strawberry Fields"
    app_msgs.push(make_service_message(service));
  }
  if (app_msgs.length === 0) {
    app_msgs.push(make_text_message("No services"));
  }
}

function bus_services_fail() {
  app_msgs.push(make_text_message("No comms"));
}

Pebble.addEventListener("appmessage",
  function(e) {
    console.log("stop_number:" + e.payload.stop_number);
    Pebble.sendAppMessage(make_text_message("Finding services"));
    app_msgs = [];
    var req = new XMLHttpRequest();
    req.open('GET', 'http://api.perthtransit.com/1/bus_stops/' + e.payload.stop_number, true);
    req.timeout = 15000;
    req.onload = function(e) {
      if (req.readyState == 4) {
        if (((req.status == 200) || (req.status == 304)) && (req.responseText.length > 0)) {
          bus_services_ok(JSON.parse(req.responseText).response);
        } else {
          bus_services_fail();
        }
        send_app_msgs();
      }
    };
    req.send(null);
  }
);