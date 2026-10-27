#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message):
	print(f"CLI check: {message}", file=sys.stderr)
	raise SystemExit(1)


def read(path):
	p = ROOT / path
	if not p.is_file():
		fail(f"missing {path}")
	return p.read_text(encoding="utf-8")


def check_index(path):
	text = read(path)
	base = (ROOT / path).parent
	for target in re.findall(r"\]\(([^):#]+)\)", text):
		if not (base / target).resolve().exists():
			fail(f"broken link in {path}: {target}")


def help_commands(text):
	return re.findall(r'strcmp\(line, "(help(?: [^"]+)?)"\)', text)


def check_unique(label, commands):
	seen = set()
	for command in commands:
		if command in seen:
			fail(f"duplicate {label} dispatch: {command}")
		seen.add(command)


check_index("docs/COMMANDS.MD")
check_index("docs/SHADOW_FEATURES.md")

companion = read("zephcore/helpers/CompanionCLIHelp.h")
repeater = read("zephcore/helpers/RepeaterCLIHelp.h")
companion_doc = read("docs/cli/companion.md")
repeater_doc = read("docs/cli/repeater.md") + read("docs/cli/bridge.md")

for token in (
	"sizeof(COMPANION_WIFI_HELP) - 1 <= 172",
	"sizeof(COMPANION_REMOTE_WIFI_HELP) <= 158",
):
	if token not in companion:
		fail(f"missing frame bound: {token}")
if "#define REPEATER_REMOTE_HELP_CAP 158" not in repeater:
	fail("repeater remote frame cap is not 158")

app_start = companion.index("companion_app_cli_help")
remote_start = companion.index("companion_remote_cli_help")
allow_start = companion.index("companion_remote_cli_command_allowed")
app_help = companion[app_start:remote_start]
remote_help = companion[remote_start:allow_start]
allowed = companion[allow_start:]

check_unique("companion app", help_commands(app_help))
check_unique("companion remote", help_commands(remote_help))
check_unique("repeater remote", help_commands(repeater))

def documented(command, text):
	if command == "help" or command in text:
		return True
	group = re.escape(command.removeprefix("help "))
	return re.search(rf"[├└]─ {group}(?:\s|$)", text) is not None


for command in set(help_commands(app_help) + help_commands(remote_help)):
	if not documented(command, companion_doc):
		fail(f"companion documentation misses {command}")
for command in set(help_commands(repeater)):
	if not documented(command, repeater_doc):
		fail(f"repeater documentation misses {command}")

for forbidden in (
	'"get prv.key"', '"set prv.key ', '"set freq ', '"get wifi.ssid"',
	'"set wifi.ssid ', '"clear stats"', '"start ota"', '"start dfu"',
	'"erase"', '"shutdown y"', '"offgrid on"', '"offgrid off"',
	'"clkreboot"', '"get gps diag"', '"get multi.acks"', '"set multi.acks ',
):
	if forbidden in allowed:
		fail(f"dangerous remote command allowed: {forbidden}")

print("CLI static checks: PASS")
