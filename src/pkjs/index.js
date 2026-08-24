function buildHelpPage() {
  return '<!doctype html><html><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>Hnefatafl Guide</title>' +
    '<style>' +
    ':root{color-scheme:dark;}' +
    '*{box-sizing:border-box;}' +
    'body{margin:0;padding:20px 16px 32px;background:#17130f;color:#f7ead0;' +
    'font-family:Georgia,"Times New Roman",serif;line-height:1.45;}' +
    '.wrap{max-width:560px;margin:0 auto;}' +
    '.hero{padding:22px 18px;background:linear-gradient(145deg,#7b2d20,#3c1712);' +
    'border:1px solid #c99349;border-radius:16px;box-shadow:0 10px 28px rgba(0,0,0,.3);}' +
    'h1{margin:0;font-size:30px;letter-spacing:.04em;}' +
    '.subtitle{margin:5px 0 0;color:#e7c98d;font-size:14px;}' +
    '.card{margin-top:14px;padding:16px;background:#262019;border:1px solid #5d4b36;' +
    'border-radius:14px;}' +
    'h2{margin:0 0 10px;color:#e4b967;font-size:18px;letter-spacing:.04em;}' +
    'ul{margin:0;padding-left:20px;}li+li{margin-top:7px;}' +
    'strong{color:#fff4dc;}' +
    '.sides{display:grid;grid-template-columns:1fr 1fr;gap:10px;}' +
    '.side{padding:12px;border-radius:10px;background:#17130f;}' +
    '.side b{display:block;color:#e4b967;margin-bottom:4px;}' +
    'button{width:100%;margin-top:18px;padding:14px;border:0;border-radius:999px;' +
    'background:#e4b967;color:#241a10;font:700 16px Georgia,serif;}' +
    '.version{text-align:center;margin-top:14px;color:#9f8a6c;font-size:12px;}' +
    '</style></head><body><main class="wrap">' +
    '<section class="hero"><h1>Hnefatafl</h1>' +
    '<p class="subtitle">The Viking game of siege and escape</p></section>' +
    '<section class="card"><h2>How to Play</h2><div class="sides">' +
    '<div class="side"><b>You</b>Control the white defenders and their king.</div>' +
    '<div class="side"><b>The Watch</b>Controls the black attackers.</div>' +
    '</div></section>' +
    '<section class="card"><h2>Controls</h2><ul>' +
    '<li><strong>Up / Down:</strong> Cycle through movable pieces.</li>' +
    '<li><strong>Select:</strong> Choose the highlighted piece.</li>' +
    '<li><strong>Up / Down:</strong> Cycle through its legal destinations.</li>' +
    '<li><strong>Select:</strong> Make the move.</li>' +
    '<li><strong>Back:</strong> Cancel a selection.</li>' +
    '<li><strong>Hold Select:</strong> Start a new game.</li>' +
    '</ul></section>' +
    '<section class="card"><h2>Objective</h2><ul>' +
    '<li><strong>Defenders win</strong> when the king reaches any red corner.</li>' +
    '<li><strong>Attackers win</strong> when they surround and capture the king.</li>' +
    '</ul></section>' +
    '<section class="card"><h2>Movement &amp; Capture</h2><ul>' +
    '<li>Every piece moves horizontally or vertically through empty squares, like a chess rook.</li>' +
    '<li>Only the king may stop on the central throne or a corner.</li>' +
    '<li>Capture an ordinary piece by sandwiching it between two enemies.</li>' +
    '<li>An empty corner or empty throne can complete a capture.</li>' +
    '<li>The king needs four attackers around him on the throne, three beside it, or an opposing pair elsewhere.</li>' +
    '</ul></section>' +
    '<button id="done" type="button">Done</button>' +
    '<div class="version">Version 1.1.0</div>' +
    '</main><script>' +
    'document.getElementById("done").addEventListener("click",function(){' +
    'document.location="pebblejs://close#";' +
    '});' +
    '</script></body></html>';
}

Pebble.addEventListener('ready', function() {
  console.log('Hnefatafl phone guide ready');
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL('data:text/html,' + encodeURIComponent(buildHelpPage()));
});
