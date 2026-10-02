import sys
import json
import urllib.request
import urllib.parse

# IMPORTANT: Change this to your ESP32's actual IP address!
ROBOT_IP = "192.168.1.60"

def notify_robot(endpoint, params=None):
    url = f"http://{ROBOT_IP}{endpoint}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    try:
        # Fire and forget (timeout quickly so we don't slow down the IDE)
        urllib.request.urlopen(urllib.request.Request(url, method="POST"), timeout=2)
    except Exception:
        pass

def main():
    if len(sys.argv) < 2:
        print("{}")
        return
        
    event = sys.argv[1]
    
    # Read the JSON context sent by Antigravity on stdin
    try:
        payload = json.load(sys.stdin)
    except Exception:
        payload = {}
        
    # Trigger the right robot animations based on the IDE event
    if event == "PreInvocation":
        # The agent is about to start thinking!
        notify_robot("/agent_thinking")
        
    elif event == "Stop":
        # The agent has finished all work and is stopping
        notify_robot("/agent_end")
        
    elif event == "PreToolUse":
        # Optional: Have the robot announce what it's doing!
        tool_name = payload.get("toolCall", {}).get("name", "")
        if tool_name:
            # Tell the robot to speak the action (e.g., "Using tool view_file")
            # notify_robot("/agent_speak", {"text": f"Using tool {tool_name}"})
            pass

    # Hooks MUST output a valid JSON object to stdout
    print(json.dumps({}))

if __name__ == "__main__":
    main()
