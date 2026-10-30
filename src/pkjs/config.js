module.exports = function(lang) {
  var i18n = {
    en: { title: "Settings", 
			datetime: "Date Time", 
			usdate: "US Date format (MM dd)", 
			hoursvibe: "Vibe on Hour",
			colors: "Colors",
			hcolors: "Header colors",
			tcolors: "Time colors",
			fcolors: "Footer colors",
			health: "Health",
			health_info: "Enable health metrics",
			steps: "Steps (distance)",
			heart_interval: "Heart Updates",
			hr_alert: "* Only on PT2",
			heart_updates_no: "Disabled",
			heart_updates_os: "OS Managed (Bilanciata)",
			heart_updates_1: "1 Minute",
			heart_updates_5: "5 Minutes",
			heart_updates_10: "10 Minutes (ECO)",
			save: "Save settings"
		},
    it: { title: "Impostazioni",
			datetime: "Date e Ora", 
			usdate: "Formato Data US(MM dd)", 
			hoursvibe: "Vibra ogni Ora",
			colors: "Colori",
			hcolors: "Colori Header",
			tcolors: "Colori Orario",
			fcolors: "Colori Footer",
			health: "Salute",
			health_info: "Abilita le metriche sulla salute",
			steps: "Passi (distanza)",
			heart_interval: "Aggiornamenti Cardio",
			hr_alert: "* Solo su PT2",
			heart_updates_no: "Disabilitati",
			heart_updates_os: "Gestiti dal sistema (Bilanciati)",
			heart_updates_1: "1 Minuto",
			heart_updates_5: "5 Minuti",
			heart_updates_10: "10 Minuti (ECO)",
			save: "Salva impostazioni"
		},
  };
  var t = i18n[lang] || i18n['en'];

  return [
		{
			"type": "heading",
			"defaultValue": t.title
		},
		{
			"type": "section",
			"items": [
				{
					"type": "heading",
					"defaultValue": t.datetime
				},
				{
					"type": "toggle",
					"messageKey": "usdate",
					"label": t.usdate,
					"defaultValue": false
				},
				{
					"type": "toggle",
					"messageKey": "hourvibe",
					"label": t.hoursvibe,
					"defaultValue": false
				}
			]
		},
		{
			"type": "section",
			"capabilities": ["COLOR"],
			"items": [
				{
					"type": "heading",
					"defaultValue": t.colors
				},
				{
					"type": "toggle",
					"messageKey": "hcolors",
					"label": t.hcolors,
					"defaultValue": true
				},
				{
					"type": "toggle",
					"messageKey": "tcolors",
					"label": t.tcolors,
					"defaultValue": false
				},{
					"type": "toggle",
					"messageKey": "fcolors",
					"label": t.fcolors,
					"defaultValue": true
				}
			]
		},
		{
			"type": "section",
			"capabilities": ["HEALTH"],
			"items": [
				{
					"type": "heading",
					"defaultValue": t.health
				},
				{
					"type": "toggle",
					"messageKey": "steps",
					"label": t.steps,
					"defaultValue": true
				},
				{
					"type": "select",
					"messageKey": "heart_interval",
					"label": t.heart_interval,
					"description": t.hr_alert,
					"defaultValue": "600", // -1 = Disabilitato, 0 = OS (Standard), 60 = 1 min, 300 = 5 min, 600 = 10 min
					"options": [
						{ "label": t.heart_updates_no, "value": "-1" },
						{ "label": t.heart_updates_os, "value": "0" },
						{ "label": t.heart_updates_1, "value": "60" },
						{ "label": t.heart_updates_5, "value": "300" },
						{ "label": t.heart_updates_10, "value": "600" }
					]
				}
			]
		},
		{
			"type": "submit",
			"defaultValue": t.save
		}
	];
};