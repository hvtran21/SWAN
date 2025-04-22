#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>

const char *ssid = "Purple Yungoos";
const char *password = "BlueGulpin200!";

WebServer server(80); // Init websever on port 80

HardwareSerial MySerial(1);

String logBuffer = ""; // Holds messages from RX

// HTML page with script and style since uploading raw files is weird
const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>S.W.A.N.</title>
  <style>
    body {
      margin: 0;
      font-family: monospace;
      display: flex;
      flex-direction: column;
      height: 100vh;
    }

    header {
      background: #f0f0f0;
      color: #000;
      padding: 15px;
      font-size: 1.5em;
      text-align: center;
      position: sticky;
      top: 0;
      border-bottom: 1px solid #ccc;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    header h1 {
      margin: 0;
      font-size: 1.2em;
      flex: 1;
      text-align: center;
    }

    #clearBtn {
      margin-left: 10px;
      margin-right: 10px;
      padding: 5px 10px;
      font-size: 0.9em;
      background-color: #d33;
      color: white;
      border: none;
      border-radius: 5px;
      cursor: pointer;
    }

    #console {
      flex: 1;
      background: #ffffff;
      color: #000000;
      padding: 15px;
      overflow-y: auto;
      white-space: pre-wrap;
      word-wrap: break-word;
      font-size: 1em;
      line-height: 1.4em;
    }
  </style>
</head>
<body>
  <header>
    <button id="clearBtn">Clear Logs</button>
    <h1>S.W.A.N. Anchor Logs</h1>
    <div style="width: 84px;"></div> <!-- To balance layout -->
  </header>

  <div id="console"></div>

  <script>
  const consoleDiv = document.getElementById('console');
  const clearBtn = document.getElementById('clearBtn');

  let lastLog = "";

  clearBtn.addEventListener('click', () => {
    consoleDiv.innerHTML = '';
    lastLog = "";
  });

  setInterval(() => {
    fetch('/logs')
      .then(response => response.text())
      .then(data => {
        if (data !== lastLog) {
          consoleDiv.innerHTML = data;
          lastLog = data;
          consoleDiv.scrollTop = consoleDiv.scrollHeight;
        }
      });
  }, 1000);
</script>
</body>
</html>
)rawliteral";

void setup()
{
  Serial.begin(9600);       // baudrate
  Serial1.begin(115200, SERIAL_8N1, 4, -1);
  WiFi.begin(ssid, password); // connect to wifi
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, []() {
  server.send_P(200, "text/html", html);
  });
  server.on("/logs", []() {
    server.send(200, "text/plain", logBuffer);
  });
  MySerial.begin(115200, SERIAL_8N1, 5, 4);
  server.begin();
}

void loop()
{
static String line = "";
while (MySerial.available()) {
  char c = MySerial.read();
  line += c;

  if (c == '\n') {
    Serial.print(line); // Optional: also print to serial monitor
    logBuffer += line + "<br>"; // HTML-friendly newline

    // Limit log buffer to ~5KB
    if (logBuffer.length() > 5000) {
      int excess = logBuffer.length() - 5000;
      logBuffer = logBuffer.substring(excess);
    }

    line = "";
  }
}

server.handleClient();
}
