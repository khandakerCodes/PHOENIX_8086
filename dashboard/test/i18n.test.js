// SPDX-License-Identifier: MIT
/** Consistency checks for the dashboard translations. */
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const i18n = require('../i18n.js');
const CODES = ['en', 'de', 'fr', 'es', 'ar'];
const locales = {};
CODES.forEach((code) => {
    const registered = {};
    const original = i18n.register;
    i18n.register = (name, locale) => { registered[name] = locale; original(name, locale); };
    require('../locales/' + code + '.js');
    i18n.register = original;
    locales[code] = registered[code];
});

const dashboard = path.join(__dirname, '..');
const placeholders = (text) => (text.match(/\{\w+\}/g) || []).sort().join(',');

test('every locale file registers under its own code with a name and a direction', () => {
    CODES.forEach((code) => {
        assert.ok(locales[code], code + ' did not register');
        assert.ok(locales[code].name.length > 0);
        assert.ok(['ltr', 'rtl'].includes(locales[code].dir));
    });
});

test('every language has exactly the English keys', () => {
    const reference = Object.keys(locales.en.strings).sort();
    CODES.forEach((code) => {
        assert.deepStrictEqual(Object.keys(locales[code].strings).sort(), reference, code);
    });
});

test('translations keep the same {placeholders} and are not empty', () => {
    Object.keys(locales.en.strings).forEach((key) => {
        CODES.forEach((code) => {
            const text = locales[code].strings[key];
            assert.ok(text.trim().length > 0, code + ' ' + key + ' is empty');
            assert.strictEqual(placeholders(text), placeholders(locales.en.strings[key]), code + ' ' + key);
        });
    });
});

test('every key used by the page exists', () => {
    const english = locales.en.strings;
    const html = fs.readFileSync(path.join(dashboard, 'index.html'), 'utf8');
    const app = fs.readFileSync(path.join(dashboard, 'app.js'), 'utf8');
    const model = fs.readFileSync(path.join(dashboard, 'model.js'), 'utf8');

    const used = new Set();
    for (const match of html.matchAll(/data-i18n(?:-title)?="([^"]+)"/g)) used.add(match[1]);
    for (const match of app.matchAll(/\bt\('([\w.]+)'/g)) used.add(match[1]);
    // keys held in tables, such as the inspector's field labels
    Object.keys(english).filter((key) => app.includes("'" + key + "'")).forEach((key) => used.add(key));
    for (const match of model.matchAll(/'(event\.\w+)'/g)) used.add(match[1]);
    // keys built at run time
    ['live', 'replay', 'demo', 'offline'].forEach((mode) => used.add('mode.' + mode));
    ['READY', 'RUNNING', 'BLOCKED', 'SLEEPING', 'TERMINATED', 'UNKNOWN'].forEach((s) => used.add('state.' + s));
    ['ivt', 'bios', 'boot', 'kernel', 'threads', 'heap', 'stack', 'far'].forEach((r) => used.add('mem.region.' + r));

    const missing = [...used].filter((key) => !(key in english) && !key.endsWith('.'));
    assert.deepStrictEqual(missing, []);

    const unused = Object.keys(english).filter((key) => !used.has(key));
    assert.deepStrictEqual(unused, [], 'keys nothing refers to');
});

test('lookup falls back to English, then to the key', () => {
    i18n.setLocale('de');
    assert.strictEqual(i18n.t('demo.run'), 'Demo starten');
    assert.strictEqual(i18n.t('no.such.key'), 'no.such.key');
    assert.strictEqual(i18n.t('event.boot', { stage: 3 }), 'Boot-Stufe 3');
    assert.strictEqual(i18n.t('event.boot'), 'Boot-Stufe {stage}');
    assert.strictEqual(i18n.setLocale('zz'), 'en');
    assert.strictEqual(i18n.match('fr-CA'), 'fr');
    assert.strictEqual(i18n.match(undefined), 'en');
});
