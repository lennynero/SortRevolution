var Clay = require('@rebble/clay');

var systemLang = (navigator.language || 'en').substring(0, 2);
var clayConfig = require('./config.js');

var clay = new Clay(clayConfig(systemLang));