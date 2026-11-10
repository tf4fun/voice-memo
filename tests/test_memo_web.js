// Browser-independent tests for the embedded web client's editing and request
// state. Uses only Node's built-ins; no frontend packages or build are needed.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {join} = require('node:path');
const vm = require('node:vm');
const source = readFileSync(join(__dirname, '../server/internal/httpapi/web/app.js'), 'utf8');

function client(status = 'ready') {
  const nodes = new Map(), timers = new Map(), windowEvents = new Map();
  let timerID = 0;
  function node(id) {
    if (nodes.has(id)) return nodes.get(id);
    const listeners = new Map();
    const obj = {
      value: '', textContent: '', hidden: false, disabled: false, dataset: {},
      classList: {toggle() {}}, focus() {},
      addEventListener(event, callback, options = {}) {
        const list = listeners.get(event) || [];
        list.push({callback, once:options.once}); listeners.set(event, list);
      },
      async fire(event, data = {preventDefault() {}}) {
        const list = listeners.get(event) || [];
        listeners.set(event, list.filter(entry => !entry.once));
        for (const entry of list) await entry.callback(data);
      },
    };
    nodes.set(id, obj); return obj;
  }
  const detail = node('main'); detail.dataset = {memoId:'recording', status};
  node('draft').value = '原转录'; node('transcript').textContent = '原转录';
  node('editor').hidden = true;
  const state = {calls:[], redirects:[], choice:'cancel', memo:{status, transcript:'原转录'}};
  state.respond = async (_path, options) => {
    if (options.method === 'PATCH') state.memo = {...state.memo, ...JSON.parse(options.body), status:'ready'};
    if (options.method === 'POST') state.memo = {...state.memo, status:'uploaded'};
    return {ok:true, status:options.method === 'DELETE' ? 204 : 200, json:async () => ({...state.memo})};
  };
  node('confirmation').showModal = () => queueMicrotask(() => {
    node('confirmation').returnValue = state.choice;
    node('confirmation').fire('close');
  });
  vm.runInNewContext(source, {
    document:{hidden:false, querySelectorAll:() => [], querySelector:() => detail, getElementById:node},
    window:{addEventListener:(type, fn) => windowEvents.set(type, fn)},
    location:{assign:path => state.redirects.push(path), reload() {}},
    Intl, Date,
    setTimeout:callback => {const id = ++timerID; timers.set(id, callback); return id;},
    clearTimeout:id => timers.delete(id),
    fetch:async (path, options) => {state.calls.push({path, ...options}); return state.respond(path, options);},
  });
  return {...state, state, node, timers, windowEvents};
}

test('editing saves literal text and updates the displayed transcript', async () => {
  const app = client();
  await app.node('edit').fire('click');
  assert.equal(app.node('editor').hidden, false);
  app.node('draft').value = '新的灵感 <script>literal</script>';
  await app.node('editor').fire('submit');
  assert.equal(app.node('editor').hidden, true);
  assert.equal(app.node('transcript').textContent, '新的灵感 <script>literal</script>');
  assert.equal(JSON.parse(app.state.calls[0].body).transcript, app.node('transcript').textContent);
  assert.match(app.node('notice').textContent, /已保存/);
});

test('failed save retains the draft and warns before leaving', async () => {
  const app = client();
  await app.node('edit').fire('click'); app.node('draft').value = '不要丢失';
  app.state.respond = async () => ({ok:false, status:500});
  await app.node('editor').fire('submit');
  assert.equal(app.node('editor').hidden, false);
  assert.equal(app.node('draft').value, '不要丢失');
  assert.equal(app.node('draft').disabled, false);
  let prevented = false;
  app.windowEvents.get('beforeunload')({preventDefault() {prevented = true;}});
  assert.equal(prevented, true);
  await app.node('cancel-edit').fire('click');
  assert.equal(app.node('transcript').textContent, '原转录');
});

test('an in-flight status poll cannot overwrite a newer manual save', async () => {
  const app = client('uploaded');
  let release;
  const oldReply = new Promise(resolve => {release = resolve;});
  const normal = app.state.respond;
  app.state.respond = (path, options) => options.method === 'GET' ? oldReply : normal(path, options);
  const callback = [...app.timers.values()][0]; app.timers.clear();
  const polling = callback();
  await app.node('edit').fire('click'); app.node('draft').value = '人工修改';
  await app.node('editor').fire('submit');
  release({ok:true, status:200, json:async () => ({status:'uploaded', transcript:'过期回复'})});
  await polling;
  assert.equal(app.node('transcript').textContent, '人工修改');
  assert.equal(app.node('main').dataset.status, 'ready');
  assert.equal(app.timers.size, 0);
});

test('retranscription and deletion require a positive dialog choice', async () => {
  const app = client();
  await app.node('transcribe').fire('click');
  await app.node('delete').fire('click');
  assert.equal(app.state.calls.length, 0);
  app.state.choice = 'confirm';
  await app.node('transcribe').fire('click');
  assert.equal(app.state.calls[0].method, 'POST');
  assert.equal(app.node('main').dataset.status, 'uploaded');
  assert.equal(app.node('transcribe').disabled, true);
  assert.equal(app.timers.size, 1);
  await app.node('delete').fire('click');
  assert.equal(app.state.calls.at(-1).method, 'DELETE');
  assert.deepEqual(app.state.redirects, ['/']);
});
