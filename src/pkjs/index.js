var Clay = require('@rebble/clay');

var clayConfig = require('./config.js');
var systemLang = (navigator.language || 'en').substring(0, 2);
var customClay = require('./custom-clay.js');

var clay = new Clay(clayConfig(systemLang), customClay);