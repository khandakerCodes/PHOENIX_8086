// SPDX-License-Identifier: MIT
/**
 * A stand-in for the browser, just large enough to run app.js under
 * Node: elements are built from the tags in index.html and record what
 * the page code does to them. It checks that the rendering code runs
 * and puts the right content in the right elements. It does not check
 * layout or appearance; only a real browser can.
 */
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const DASHBOARD = path.join(__dirname, '..');
const SCRIPTS = ['i18n.js', 'locales/en.js', 'locales/de.js', 'locales/fr.js', 'locales/es.js',
                 'locales/ar.js', 'model.js', 'demo.js', 'app.js'];

function camel(name) {
    return name.replace(/-(\w)/g, (_, letter) => letter.toUpperCase());
}

function makeElement(tagName, attributes) {
    const classes = new Set((attributes.class || '').split(/\s+/).filter(Boolean));
    const listeners = {};
    const element = {
        tagName: tagName.toUpperCase(),
        id: attributes.id || '',
        children: [],
        dataset: {},
        style: {},
        hidden: false,
        disabled: 'disabled' in attributes,
        value: '',
        textContent: '',
        title: '',
        placeholder: '',
        tabIndex: 0,
        scrollTop: 0,
        scrollHeight: 0,
        clientHeight: 0,
        listeners: listeners,
        classList: {
            add: (...names) => names.forEach((n) => classes.add(n)),
            remove: (...names) => names.forEach((n) => classes.delete(n)),
            toggle: (name, on) => ((on === undefined ? !classes.has(name) : on) ? classes.add(name) : classes.delete(name)),
            contains: (name) => classes.has(name),
        },
        appendChild(child) { this.children.push(child); return child; },
        replaceChildren(...nodes) { this.children.length = 0; this.children.push(...nodes); },
        addEventListener(type, handler) { listeners[type] = handler; },
        querySelector() { return this._inner || (this._inner = makeElement('span', {})); },
    };
    Object.defineProperty(element, 'className', {
        get: () => [...classes].join(' '),
        set: (value) => { classes.clear(); String(value).split(/\s+/).filter(Boolean).forEach((n) => classes.add(n)); },
    });
    Object.keys(attributes).forEach((name) => {
        if (name.startsWith('data-')) {
            element.dataset[camel(name.slice(5))] = attributes[name];
        }
    });
    return element;
}

/** Build one element per opening tag in the page. */
function parse(html) {
    const elements = [];
    const tagPattern = /<([a-z][a-z0-9]*)((?:\s+[a-z0-9-]+(?:="[^"]*")?)*)\s*>/gi;
    let match;
    while ((match = tagPattern.exec(html)) !== null) {
        const attributes = {};
        const attributePattern = /([a-z0-9-]+)(?:="([^"]*)")?/gi;
        let attribute;
        while ((attribute = attributePattern.exec(match[2])) !== null) {
            attributes[attribute[1]] = attribute[2] === undefined ? '' : attribute[2];
        }
        elements.push(makeElement(match[1], attributes));
    }
    return elements;
}

/**
 * Load the dashboard. Returns handles for the test: the elements by id,
 * the WebSocket the page opened, and flush() to run its pending timers.
 */
function load(options) {
    const settings = Object.assign({ language: 'en', search: '', stored: null }, options);
    const html = fs.readFileSync(path.join(DASHBOARD, 'index.html'), 'utf8');
    const elements = parse(html);
    const byId = {};
    elements.forEach((element) => { if (element.id) byId[element.id] = element; });

    const timers = [];
    const storage = settings.stored ? { 'phoenix-locale': settings.stored } : {};
    const page = { sockets: [], sent: [], byId: byId, elements: elements };
    let ready = null;

    const documentElement = makeElement('html', {});
    const context = {
        console: console,
        URLSearchParams: URLSearchParams,
        location: { hostname: 'localhost', search: settings.search },
        navigator: { language: settings.language },
        localStorage: {
            getItem: (key) => (key in storage ? storage[key] : null),
            setItem: (key, value) => { storage[key] = String(value); },
        },
        setTimeout: (handler) => { timers.push(handler); return timers.length; },
        setInterval: (handler) => { page.interval = handler; return 1; },
        clearInterval: () => { page.interval = null; },
        document: {
            documentElement: documentElement,
            getElementById(id) {
                if (!byId[id]) throw new Error('index.html has no element with id "' + id + '"');
                return byId[id];
            },
            createElement: (tag) => makeElement(tag, {}),
            createTextNode: (text) => ({ textContent: text }),
            querySelectorAll(selector) {
                if (selector.startsWith('.')) {
                    return elements.filter((e) => e.classList.contains(selector.slice(1)));
                }
                const attribute = /^\[data-([a-z0-9-]+)\]$/.exec(selector);
                if (attribute) {
                    return elements.filter((e) => camel(attribute[1]) in e.dataset);
                }
                throw new Error('fake DOM does not support selector ' + selector);
            },
            addEventListener: (type, handler) => { if (type === 'DOMContentLoaded') ready = handler; },
        },
        WebSocket: function (url) {
            this.url = url;
            this.send = (data) => page.sent.push(JSON.parse(data));
            this.close = () => {};
            page.sockets.push(this);
        },
    };
    context.self = context;
    vm.createContext(context);
    SCRIPTS.forEach((file) => {
        const source = fs.readFileSync(path.join(DASHBOARD, file), 'utf8')
            .replace("typeof module === 'object' && module.exports", 'false');
        vm.runInContext(source, context, { filename: file });
    });

    page.flush = () => {
        let guard = 0;
        while (timers.length && guard++ < 500) {
            timers.shift()();
        }
    };
    page.socket = () => page.sockets[page.sockets.length - 1];
    page.receive = (message) => page.socket().onmessage({ data: JSON.stringify(message) });
    page.i18n = context.PhoenixI18n;
    page.documentElement = documentElement;

    ready();
    page.flush();
    return page;
}

function loadCapture() {
    return fs.readFileSync(path.join(__dirname, 'fixtures', 'session.jsonl'), 'utf8')
        .split('\n').filter(Boolean).slice(1).map((line) => JSON.parse(line).msg);
}

module.exports = { load: load, loadCapture: loadCapture, DASHBOARD: DASHBOARD };
