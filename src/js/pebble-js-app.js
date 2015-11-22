/* * * * * * *
 * GEOCALCS  *
 * * * * * * */

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
  if (t < -1) {
    t = -1;
  }
  if (t > 1) {
    t = 1;
  }
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
    if (c < -1) {
      c = -1;
    }
    if (c > 1) {
      c = 1;
    }
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
  if (c >= 360.0) {
    c -= 360.0;
  }
  c = geocalcs.bearingLabels[Math.floor(c / 22.5)];
  if (!c) {
    c = "-";
  }
  return c;
};

/* * * * * * * * * *
 * Perth Bus Times *
 * * * * * * * * * */

/* http://doc.perthtransit.com/ */

var app_msgs = [];

function queue_app_msg(msg) {
  app_msgs.push(msg);
}

function send_app_msgs(e) {
  var msg;
  if (app_msgs.length > 0) {
    msg = app_msgs.shift();
    //console.log("send_app_msgs:{identifier:\"" + msg.identifier + "\", title:\"" + msg.title + "\", subtitle:\"" + msg.subtitle + "\"}");
    Pebble.sendAppMessage({
      identifier: msg.identifier,
      title: msg.title.substr(0, 20),
      subtitle: msg.subtitle.substr(0, 30),
      icon: msg.icon
    }, send_app_msgs);
  }
}

function make_text_message(msg) {
  return {
    identifier: msg[0],
    title: msg.substr(1),
    subtitle: "",
    icon: ""
  };
}

function queue_stops(pos, api, req) {
  var dst, msg, rsp, stopinfo, typ;
  if (api == "bus_stops" ) {
    rsp = "!Bus stops";
  } else {
    rsp = "!Train stations";
  }
  queue_app_msg(make_text_message(rsp));
  rsp = JSON.parse(req.responseText);
  msg = null;
  for (var idx in rsp.response) {
    stopinfo = rsp.response[idx];
    // categorise stop
    if ((stopinfo.identifier >= 1) && (stopinfo.identifier < 90000)) {
      typ = "B"; // bus stop
    } else if ((stopinfo.identifier >= 90000) && (stopinfo.identifier < 99998)) {
      typ = "P"; // train station platform
    } else if ((stopinfo.identifier >= 99998) && (stopinfo.identifier <= 99999)) {
      typ = "F"; // ferry
    } else {
      typ = "T"; // train station
    }
    if (typ != "P") {
      msg = {identifier: stopinfo.identifier, title: "", icon: typ};
      if (stopinfo.stop_number) {
        msg.title = stopinfo.stop_number + " ";
      }
      dst = Math.floor(geocalcs.earthDistance(pos, stopinfo) * 1000.0); // metres
      if (dst < 1000.0) {
        msg.title += Math.round(dst / 10.0) * 10.0 + "m"; // nearest 10m
      } else {
        msg.title += Math.round(dst / 100.0) / 10.0 + "km"; // nearest 0.1km
      }
      // compass direction, eg "E"
      msg.title += " " + geocalcs.compassBearing(geocalcs.bearing(pos, stopinfo));
      // abbreviate message
      msg.subtitle = stopinfo.name.replace(" Before ", " b ").replace(" After ", " a ");
      queue_app_msg(msg);
    }
  }
  if (!msg) {
    queue_app_msg(make_text_message(" No stops nearby"));
  }
}

function send_stops_near(pos, api, attempt) {
  var req;
  req = new XMLHttpRequest();
  req.open("GET", "http://api.perthtransit.com/1/" + api + "?near=" + pos.lat + "," + pos.lng, true);
  req.timeout = 5000;
  req.onload = function(e) {
    queue_stops(pos, api, req);
    if (api == "bus_stops" ) {
      send_app_msgs();
    } else {
      send_stops_near(pos, "bus_stops", 3);
    }
  };
  req.onerror = function(e) {
    console.log("send_stops_near.req.onerror");
    // request error
    if (attempt > 0) {
      send_stops_near(pos, api, attempt - 1);
    } else {
      queue_app_msg(make_text_message("$No comms"));
      send_app_msgs();
    }
  };
  req.ontimeout = req.onerror;
  req.send(null);
}

function position_ok(position) {
  var pos = {
    lat: position.coords.latitude, lng: position.coords.longitude // <- FOR RELEASE
    //lat: -31.902740, lng: 115.907434 // <- FOR DEMO (Collier Rd photo location)
  };
  //console.log("position_ok:" + pos.lat + "," + pos.lng);
  Pebble.sendAppMessage(make_text_message("$Finding stops"));
  app_msgs = [];
  send_stops_near(pos, "train_stations", 3);
}

function position_fail(error) {
  Pebble.sendAppMessage(make_text_message("$No location"));
}

Pebble.addEventListener("ready",
  function(e) {
    setTimeout(function() {
      Pebble.sendAppMessage(make_text_message("$Locating"));
      navigator.geolocation.getCurrentPosition(position_ok, position_fail,
        {enableHighAccuracy: true, timeout: 60 * 1000, maximumAge: 1 * 60 * 1000});
    }, 10);
  }
);

function queue_services(response) {
  var msg, service;
  // Common to Trains and Bus Stops:
  //   response.lat = -31.9029238888889
  //   response.lng = 115.909585
  // For Trains (identifier starts with a-z):
  //   response.identifier = "perth"
  //   response.name = "Perth"
  // For Bus Stops (identifier starts with 0-9):
  //   response.identifier = "15832"
  //   response.name = "Collier Rd After Priestley St"
  //   response.stop_number = "15832"
  if (response.stop_number) {
    queue_app_msg(make_text_message("!" + response.stop_number));
  } else {
    queue_app_msg(make_text_message("!" + response.name));
  }
  msg = null;
  for (var idx in response.times) {
    service = response.times[idx];
    // For Trains:
    //   service.time = "21:11"
    //   service.cars = 2
    //   service.line = "Fremantle"
    //   service.on_time = true
    //   service.pattern = null or "K"
    //   service.platform = 7
    //   service.status = "On Time"
    // For Bus Stops:
    //   service.time = "21:11"
    //   service.route = "955"
    //   service.destination = "To Ellenbrook"
    if (service.pattern) {
      service.line += "(" + service.pattern + ")";
    }
    msg = {identifier: " ", icon: ""};
    if (response.stop_number) {
      // Bus Stops
      msg.title = service.time + " +#:## " + service.route;
      msg.subtitle = service.destination;
    } else {
      // Trains
      msg.title = service.time + " +#:## P" + service.platform;
      msg.subtitle = service.status.replace(" min", "m").replace(" delay", " dly").replace("On Time", "On time") + " " + service.line + " " + service.cars + "car";
    }
    queue_app_msg(msg);
  }
  if (!msg) {
    queue_app_msg(make_text_message(" No services"));
  }
}

function send_services_at(api, attempt) {
  app_msgs = [];
  var req = new XMLHttpRequest();
  req.open("GET", "http://api.perthtransit.com/1/" + api, true);
  req.timeout = 5000;
  req.onload = function(e) {
    queue_services(JSON.parse(req.responseText).response);
    send_app_msgs();
  };
  req.onerror = function(e) {
    console.log("send_services_at.req.onerror");
    if (attempt > 0) {
      send_services_at(api, attempt - 1);
    } else {
      queue_app_msg(make_text_message("$No comms"));
      send_app_msgs();
    }
  };
  req.ontimeout = req.onerror;
  req.send(null);
}

Pebble.addEventListener("appmessage",
  function(e) {
    var api;
    //console.log("identifier:" + e.payload.identifier);
    Pebble.sendAppMessage(make_text_message("$Finding services"));
    if ((e.payload.identifier >= 1) && (e.payload.identifier <= 99999)) {
      api = "bus_stops";
    } else {
      api = "train_stations";
    }
    send_services_at(api + "/" + e.payload.identifier, 3);
  }
);