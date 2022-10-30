String indexhtml = \
"<!DOCTYPE html>\n" \
"<html>\n" \
"<head>\n" \
"  <title>Pytes E-BOX 48100-R to MQTT configuration</title></title>\n" \
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n" \
"  <link rel=\"icon\" href=\"data:,\">\n" \
"  <link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\">\n" \
"</head>\n" \
"<body>\n" \
"  <div class=\"topnav\">\n" \
"    <h1>Pytes E-BOX 48100-R to MQTT configuration</h1>\n" \
"  </div>\n" \
"  <div class=\"content\">\n" \
"    <div class=\"card-grid\">\n" \
"      <div class=\"card\">\n" \
"        <form action=\"/\" method=\"POST\">\n" \
"          <div class=\"form\">\n" \
"            <label for=\"ssid\">SSID</label>\n" \
"            <input type=\"text\" id =\"ssid\" name=\"ssid\" value=\"%SSID%\"><br>\n" \
"            <label for=\"password\">Password</label>\n" \
"            <input type=\"password\" id =\"password\" name=\"password\" value=\"%PASSWORD%\"><br>\n" \
"            <br/>\n" \
"            <label for=\"mqttip\">MQTT IP</label>\n" \
"            <input type=\"text\" id =\"mqttip\" name=\"mqttip\" value=\"%MQTTIP%\"><br>\n" \
"            <label for=\"mqttuser\">MQTT User</label>\n" \
"            <input type=\"text\" id =\"mqttuser\" name=\"mqttuser\" value=\"%MQTTUSER%\"><br>\n" \
"            <label for=\"mqttpassword\">MQTT Password</label>\n" \
"            <input type=\"password\" id =\"mqttpassword\" name=\"mqttpassword\" value=\"%MQTTPASSWORD%\"><br>\n" \
"            <br/>\n" \
"            <label for=\"telnetpassword\">Telnet Password</label>\n" \
"            <input type=\"password\" id =\"telnetpassword\" name=\"telnetpassword\" value=\"%TELNETPASSWORD%\"><br>\n" \
"            <br/>\n" \
"            <label for=\"rackcount\">E-BOX Racks</label>\n" \
"            <input type=\"number\" id =\"rackcount\" name=\"rackcount\" value=\"%RACKCOUNT%\"><br>\n" \
"            <br/>            \n" \
"            <label for=\"ip\">DHCP</label>\n" \
"            <input type=\"checkbox\" id =\"dhcp\" name=\"dhcp\" %DHCP%><br>\n" \
"            <div class=\"hidden-content\">\n" \
"                <label for=\"ip\">IP Address</label>\n" \
"                <input type=\"text\" id =\"ip\" name=\"ip\" value=\"%IP%\"><br>\n" \
"                <label for=\"ip\">Subnet</label>\n" \
"                <input type=\"text\" id =\"subnet\" name=\"subnet\" value=\"%SUBNET%\"><br>            \n" \
"                <label for=\"ip\">DNS 1</label>\n" \
"                <input type=\"text\" id =\"dns1\" name=\"dns1\" value=\"%DNS1%\"><br>            \n" \
"                <label for=\"ip\">DNS 2</label>\n" \
"                <input type=\"text\" id =\"dns2\" name=\"dns2\" value=\"%DNS2%\"><br>            \n" \
"                <label for=\"gateway\">Gateway Address</label>\n" \
"                <input type=\"text\" id =\"gateway\" name=\"gateway\" value=\"%GATEWAY%\"><br>\n" \
"            </div>\n" \
"            <div class=\"submit\"><input type =\"submit\" value =\"Submit\"></div>\n" \
"          </div>\n" \
"        </form>\n" \
"      </div>\n" \
"    </div>\n" \
"  </div>\n" \
"</body>\n" \
"</html>\n";
