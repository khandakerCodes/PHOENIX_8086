// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — Dashboard Translations
 *
 * Each file in locales/ registers one language: a name, a writing
 * direction and a table of strings. English is the reference; a key
 * missing from another language falls back to English.
 *
 *   PhoenixI18n.t('event.created', { thread: '[2] demo-A', priority: 5 })
 *
 * Text in {braces} is replaced by the matching parameter.
 */
(function (root, factory) {
    if (typeof module === 'object' && module.exports) {
        module.exports = factory();
    } else {
        root.PhoenixI18n = factory();
    }
}(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    const FALLBACK = 'en';
    const locales = {};
    let current = FALLBACK;

    function register(code, locale) {
        locales[code] = locale;
    }

    function available() {
        return Object.keys(locales).map(function (code) {
            return { code: code, name: locales[code].name };
        });
    }

    /** Pick the best registered language for a browser language tag such as "de-AT". */
    function match(tag) {
        const code = String(tag || '').toLowerCase().split('-')[0];
        return locales[code] ? code : FALLBACK;
    }

    function setLocale(code) {
        current = locales[code] ? code : FALLBACK;
        return current;
    }

    function locale() {
        return current;
    }

    function direction() {
        return (locales[current] && locales[current].dir) || 'ltr';
    }

    function t(key, params) {
        const table = locales[current] ? locales[current].strings : {};
        const fallback = locales[FALLBACK] ? locales[FALLBACK].strings : {};
        let text = table[key];
        if (text === undefined) text = fallback[key];
        if (text === undefined) return key;
        return text.replace(/\{(\w+)\}/g, function (whole, name) {
            return params && params[name] !== undefined ? String(params[name]) : whole;
        });
    }

    /** Fill every element that carries data-i18n (text) or data-i18n-title. */
    function apply(doc) {
        doc.querySelectorAll('[data-i18n]').forEach(function (node) {
            node.textContent = t(node.dataset.i18n);
        });
        doc.querySelectorAll('[data-i18n-title]').forEach(function (node) {
            node.title = t(node.dataset.i18nTitle);
        });
        doc.documentElement.lang = current;
        doc.documentElement.dir = direction();
    }

    return {
        register: register, available: available, match: match,
        setLocale: setLocale, locale: locale, direction: direction,
        t: t, apply: apply, FALLBACK: FALLBACK,
    };
}));
