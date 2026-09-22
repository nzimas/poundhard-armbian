/*
 * Module resolve hook: the Schwung shared library, served by phhost.
 *
 * Appliance ui.js files import Schwung's helpers by absolute path, e.g.
 *   import { setLED } from '/data/UserData/move-anything/shared/input_filter.mjs';
 * Under Armbian there is no Schwung install to find them in. phhost ships the
 * same files (shared/, MIT, unmodified) and this hook points those imports at
 * them. A name we do not ship fails to resolve — it is not looked up anywhere
 * else, so an appliance can never silently run against a stale Schwung copy.
 */
const SCHWUNG_SHARED = [
    '/data/UserData/move-anything/shared/',
    '/data/UserData/schwung/shared/',
];
const OURS = new URL('./shared/', import.meta.url);

export async function resolve(specifier, context, next) {
    const path = specifier.startsWith('file://') ? new URL(specifier).pathname : specifier;
    for (const pre of SCHWUNG_SHARED) {
        if (path.startsWith(pre)) {
            return next(new URL(path.slice(pre.length), OURS).href, context);
        }
    }
    return next(specifier, context);
}
