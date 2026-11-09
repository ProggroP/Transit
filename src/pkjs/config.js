// Clay-Konfigurationsseite. Nutzersichtbare Texte englisch.
// messageKey muss Zeichen fuer Zeichen zu package.json passen, die Werte der
// Auswahllisten zu THEME_* und INFO_* in transit.c.
module.exports = [
  {
    "type": "heading",
    "defaultValue": "Transit"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Display"
      },
      {
        "type": "select",
        "messageKey": "THEME",
        "label": "Colour theme",
        "defaultValue": "6",
        "options": [
          { "label": "Transit (yellow on red)", "value": "6" },
          { "label": "Classic Amber",        "value": "0" },
          { "label": "Neon Green",           "value": "1" },
          { "label": "High-Contrast Cyan",   "value": "2" },
          { "label": "Bi-Color Flip Dot",    "value": "3" },
          { "label": "Monochrome White",     "value": "4" },
          { "label": "Monochrome Inverse",   "value": "5" },
          { "label": "Custom",               "value": "7" }
        ]
      },
      {
        "type": "color",
        "messageKey": "CUSTOM_SCREEN",
        "label": "Background",
        "defaultValue": "000000"
      },
      {
        "type": "color",
        "messageKey": "CUSTOM_PLATE",
        "label": "Plates",
        "defaultValue": "FF0000"
      },
      {
        "type": "color",
        "messageKey": "CUSTOM_FG",
        "label": "Digits and letters",
        "defaultValue": "FFFF00"
      },
      {
        "type": "select",
        "messageKey": "INFO_MODE",
        "label": "Third line",
        "defaultValue": "0",
        "options": [
          { "label": "Date (MON 14)",        "value": "0" },
          { "label": "Steps",                "value": "1" },
          { "label": "Heart rate",           "value": "2" },
          { "label": "Low / high temperature", "value": "3" },
          { "label": "Off (time fills the screen)", "value": "4" }
        ]
      },
      {
        "type": "text",
        "defaultValue": "Heart rate needs a watch with a heart-rate sensor; on Pebble Round 2 the line shows --."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Temperature"
      },
      {
        "type": "select",
        "messageKey": "TEMP_UNIT",
        "label": "Temperature unit",
        "defaultValue": "auto",
        "options": [
          { "label": "Automatic (phone region)", "value": "auto" },
          { "label": "Celsius",                  "value": "celsius" },
          { "label": "Fahrenheit",               "value": "fahrenheit" }
        ]
      },
      {
        "type": "text",
        "defaultValue": "Low and high for today come from Open-Meteo.com, updated every 30 minutes using your phone's location."
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
