import http.server
import socketserver
import json
import urllib.parse
import os

PORT = 8000
LOGFILE = "log.csv"


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path == "/log/add":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)
            data = json.loads(body.decode())

            # append to log.csv
            with open(LOGFILE, "a") as f:
                f.write(f"{data['timestamp']};{data['userId']};{data['value']:.3f}\n")

            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b"{\"status\":\"ok\"}")
            return

        return super().do_POST()

    def do_GET(self):
        if self.path.startswith("/log/download"):
            # If no log file exists
            if not os.path.exists(LOGFILE):
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(b"No log data")
                return

            # Parse query parameters
            parsed = urllib.parse.urlparse(self.path)
            query = urllib.parse.parse_qs(parsed.query)

            users_filter = None
            if "user" in query:
                users_filter = [query["user"][0]]
            elif "users" in query:
                users_filter = query["users"][0].split(",")

            lines = []
            with open(LOGFILE, "r") as f:
                for line in f:
                    if users_filter:
                        parts = line.strip().split(";")
                        if len(parts) >= 3:
                            user_id = parts[1]
                            if user_id not in users_filter:
                                continue
                    lines.append(line)

            # Return CSV
            csv_data = "".join(lines).encode()

            self.send_response(200)
            self.send_header("Content-Type", "text/csv")
            self.send_header("Content-Disposition", "attachment; filename=log.csv")
            self.end_headers()
            self.wfile.write(csv_data)
            return

        return super().do_GET()

    def do_DELETE(self):
        if self.path == "/log/clear":
            if os.path.exists(LOGFILE):
                os.remove(LOGFILE)

            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Cleared")
            return

        return super().do_DELETE()


with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print(f"Mock ESP32 server running at http://localhost:{PORT}")
    httpd.serve_forever()

