"""Host tier: the BLE setup page (components/ble_setup/web/index.html) in a
headless Chromium against a simulated device: it draws the form the field
rules describe and writes what the browser's checks let through. Skips without
node and Playwright."""

import json
import os
import pathlib
import shutil
import subprocess
import urllib.parse as up

import pytest

PAGE = pathlib.Path(__file__).resolve().parents[2] / "components/ble_setup/web/index.html"


def enc(d):
    return up.urlencode(d, quote_via=up.quote)


SETTINGS = enc([("ssid", ""), ("password_set", "0"), ("mail_port", ""), ("setup", "std,cycle"),
                ("every", "60"), ("limit", ""), ("alerts", "off"), ("token_set", "1")])
RULES = enc([
    ("ssid", enc({"section": "WiFi", "label": "Network name", "required": "1"})),
    ("password", enc({"label": "Password", "minlength": "8"})),
    ("mail_port", enc({"section": "Email", "intro": "All or none.", "label": "Port",
                       "options": ",465,587"})),
    ("setup", enc({"section": "Readings", "label": "Setup", "required": "1",
                   "options": "std,fine", "labels": "Standard|Frequent", "extras": "quick,cycle",
                   "extra_labels": "Quick check|Cycle check", "extras_intro": "Run first:"})),
    ("every", enc({"label": "Every", "type": "number", "min": "1", "max": "60",
                   "unit": "minutes"})),
    ("limit", enc({"label": "Pressure below", "type": "number", "step": "0.01", "min": "300",
                   "max": "1100", "unit": "mbar"})),
    ("alerts", enc({"label": "Alerts", "options": "off,on"})),
    ("token", enc({"label": "Token"})),
])

SCRIPT = r"""
const { chromium } = require('playwright');
const [page_path, settings, rules, steps] = process.argv.slice(2);
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const writes = [];
  await page.exposeFunction('logWrite', (w) => writes.push(w));
  await page.addInitScript(({ settings, rules }) => {
    const enc = new TextEncoder(), dec = new TextDecoder();
    let mode = 'read', off = 0, staged = '';
    const dv = (s) => new DataView(enc.encode(s).buffer);
    const ch = (uuid) => ({
      async readValue() {
        if (uuid.startsWith('6e0a0002')) return dv('fw=1&host=t&state=PROVISIONING');
        if (uuid.startsWith('6e0a0003')) {
          return new DataView(enc.encode(mode === 'form' ? rules : settings).slice(off, off + 512).buffer);
        }
        return dv('ok');
      },
      async writeValueWithResponse(v) {
        const s = dec.decode(v);
        if (uuid.startsWith('6e0a0003')) { staged += s; return; }
        const m = s.match(/^(read|form)=(\d+)$/);
        if (m) { mode = m[1]; off = +m[2]; return; }
        if (s === 'apply') { window.logWrite(staged); staged = ''; }
      },
    });
    const device = { name: 'T', addEventListener() {}, gatt: { connected: false,
      async connect() { this.connected = true;
        return { async getPrimaryService() { return { async getCharacteristic(u) { return ch(u); } }; } }; },
      disconnect() { this.connected = false; } } };
    navigator.bluetooth = { async requestDevice() { return device; } };
  }, { settings, rules });
  await page.goto('file://' + page_path);
  await page.click('#connect');
  await page.waitForSelector('#form:not([hidden])');
  const drawn = await page.evaluate(() => ({
    headings: [...document.querySelectorAll('#fields h2')].map((h) => h.textContent),
    intros: [...document.querySelectorAll('#fields p.intro')].map((p) => p.textContent),
    units: [...document.querySelectorAll('#fields .unit span')].map((s) => s.textContent),
    port: [...document.getElementById('f-mail_port').options].map((o) => [o.value, o.text]),
    alerts: [...document.getElementById('f-alerts').options].map((o) => o.value),
    setup: [...document.getElementById('f-setup').options].map((o) => [o.value, o.text]),
    setupValue: document.getElementById('f-setup').value,
    extras: [...document.querySelectorAll('input[data-extra]')].map((c) => [c.value, c.checked]),
    starred: [...document.querySelectorAll('label[data-req="1"]')].map((l) => l.htmlFor),
  }));
  for (const [sel, value] of JSON.parse(steps)) {
    if (value === true) await page.check(sel); else if (value === false) await page.uncheck(sel);
    else await page.fill(sel, value);
  }
  await page.click('#save');
  await page.waitForTimeout(300);
  console.log(JSON.stringify({ drawn, writes }));
  await browser.close();
})();
"""


@pytest.fixture(scope="module")
def run_page(tmp_path_factory):
    node = shutil.which("node")
    if not node:
        pytest.skip("no node")
    root = subprocess.run(["npm", "root", "-g"], capture_output=True, text=True).stdout.strip()
    script = tmp_path_factory.mktemp("page") / "run.js"
    script.write_text(SCRIPT)
    env = {**os.environ, "NODE_PATH": root}
    probe = subprocess.run([node, "-e", "require('playwright')"], env=env, capture_output=True)
    if probe.returncode:
        pytest.skip("no Playwright for node")

    def run(steps):
        out = subprocess.run([node, str(script), str(PAGE), SETTINGS, RULES, json.dumps(steps)],
                             env=env, capture_output=True, text=True, timeout=60)
        assert out.returncode == 0, out.stderr
        return json.loads(out.stdout.strip().splitlines()[-1])
    return run


def test_page_draws_the_described_form(run_page):
    d = run_page([])["drawn"]
    assert d["headings"] == ["WiFi", "Email", "Readings"]
    assert d["intros"] == ["All or none.", "Run first:"]
    assert d["units"] == ["minutes", "mbar"]
    assert d["port"] == [["", "-"], ["465", "465"], ["587", "587"]]  # none allowed: listed
    assert d["alerts"] == ["off", "on"]                               # no empty choice
    assert d["setup"] == [["std", "Standard"], ["fine", "Frequent"]]
    assert d["setupValue"] == "std"
    assert d["extras"] == [["quick", False], ["cycle", True]]
    assert d["starred"] == ["f-ssid", "f-setup"]


def test_page_writes_choice_extras_and_decimal_step(run_page):
    # A pressure limit with two decimals passes only with the rule's step.
    w = run_page([["#f-ssid", "net"], ["#f-password", "password1"],
                  ['input[data-extra="setup"][value="quick"]', True],
                  ['input[data-extra="setup"][value="cycle"]', False],
                  ["#f-limit", "1001.25"]])["writes"]
    assert len(w) == 1
    body = dict(up.parse_qsl(w[0], keep_blank_values=True))
    assert body["setup"] == "std,quick"
    assert body["limit"] == "1001.25"
    assert "token" not in body  # a stored secret left empty is kept


def test_page_writes_nothing_while_a_field_fails(run_page):
    assert run_page([["#f-ssid", "net"], ["#f-every", "61"]])["writes"] == []
