/* Headless smoke test for preview/index.html: run its inline script against a
 * minimal DOM stub and exercise the page-turn handlers.
 * Usage: node tools/check_preview.js
 */
const fs = require('fs');
const path = require('path');

const html = fs.readFileSync(path.join(__dirname, '..', 'preview', 'index.html'), 'utf8');
const a = html.lastIndexOf('<script>');
const b = html.lastIndexOf('</script>');
if (a < 0 || b < 0) throw new Error('no inline script found');
const code = html.slice(a + 8, b);

function mkEl(id) {
  return {
    id, value: '', textContent: '', innerHTML: '', className: '', title: '', src: '',
    style: {}, children: [], offsetWidth: 1,
    classList: { add() {}, remove() {} },
    appendChild(c) { this.children.push(c); },
    getBoundingClientRect() { return { left: 0, top: 0, width: 758, height: 1024 }; },
    addEventListener() {}, onclick: null, onchange: null
  };
}
const els = {};
global.window = {};
global.document = {
  getElementById(id) { return els[id] || (els[id] = mkEl(id)); },
  createElement(tag) { const e = mkEl(tag); e.tag = tag; return e; },
  addEventListener() {}
};
global.alert = () => {};

eval(code);
if (!els.shot.src.startsWith('data:image/png;base64,')) throw new Error('first frame not inlined');
console.log('initial render OK, inline frame bytes =', els.shot.src.length);
console.log('  counter =', els.counter.textContent);

els.next.onclick();
console.log('next page OK, counter =', els.counter.textContent);
els.prev.onclick();
console.log('prev page OK, counter =', els.counter.textContent);

els.screen.onclick({ clientX: 700, clientY: 500 });
console.log('right-zone tap OK, counter =', els.counter.textContent);
els.screen.onclick({ clientX: 50, clientY: 500 });
console.log('left-zone tap OK, counter =', els.counter.textContent);

els.device.value = 'k3';
els.device.onchange.call(els.device);
console.log('device switch OK, counter =', els.counter.textContent);
console.log('preview smoke test: PASS');
