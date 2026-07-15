# PocketInkJS embedded runtime

PocketInkJS is a compact JavaScript subset for ESP32 applications. The runtime is
implemented in the adjacent single header, `PocketInkJS.h`; callers provide its
workspace and register native functions. The Files app adapter is implemented
in `src/sys/pocketinkjs_app.cpp`. It targets ESP32 devices with external PSRAM.

JavaScript files on the SD card are opened from Files. Reopening a file
destroys the previous VM and evaluates the new contents, providing the
hot-reload boundary.

## Application lifecycle

Scripts can define these callbacks:

- `onStart()` and `onStop()`
- `onDraw()`
- `onUpdate()`; a truthy return requests a redraw
- `onTouch(event)`, or `onTouchDown`, `onTouchMove`, and `onTouchUp`
- `onMenu`, `onMenuDouble`, `onMenuLong`, and corresponding power callbacks
- `onSave()` and `onRestore(string)`

Touch events contain `type`, `x`, and `y`. Drawing normally belongs in
`onDraw`; touch and timer callbacks update state and return truthy when a
redraw is needed.

Long-pressing MENU requests the shell's standard quit confirmation unless an
`onMenuLong` callback returns a truthy value to handle the press itself.

## Timers, promises, and networking

Use timers instead of `time.delay`:

```js
const timeout = setTimeout(callback, milliseconds, ...args);
const interval = setInterval(callback, milliseconds, ...args);
clearTimeout(timeout);
clearInterval(interval);
```

There are at most 16 active timers. Timer callbacks and Promise continuations
run serially on the app/VM core. The adapter drains up to 32 Promise reactions
after each callback cycle so ordinary chains cannot outgrow the event loop.
Promise handlers run in order, after the current app callback returns, and only
after their Promise resolves or rejects. A pending file picker, capture, or
network request cannot call its `.then(...)` handler early. Hardware and
network work may run elsewhere, but JavaScript handlers always return to the
VM task. Each Promise supports one continuation branch; build longer flows by
chaining the Promise returned from `.then(...)`.
`fetch(url, options?)` returns a Promise and
supports `method`, string `body`, short-string `headers`, `timeout` from
100–30,000 ms, and `maxBytes` from 1–65,536 bytes. Responses provide
`ok`, `status`, `statusText`, `url`, `headers`, `bodyUsed`, `text()`, and
`json()`. HTTP errors resolve with `ok: false`; connection, timeout,
allocation, and size failures reject.

Network and TLS work runs on a core-0 worker and never touches VM values.
Settlement returns to the app core. A pending `await` pumps completions but
holds the current callback, so Promise chains are preferable for responsive
UI code. The older synchronous `http.get(url, maxBytes?)` remains for
compatibility.

## Host APIs

The runtime exposes `console.log`, `time.millis`, `network.connected`,
`screen.width/height`, `app.id/path`, `app.setWakeLock(enabled)`, and
`app.configGet/configSet`. Diagnostic scripts can sample the allocation-free
`PocketInkJS.heapUsed()`, `PocketInkJS.heapFree()`, and `PocketInkJS.heapPeak()` counters.

UI bindings include text and primitive drawing (`ui.setFont`, text sizing and
color, cursor, `print`, pixels, lines, rectangles, and screen fill), plus:

- `ui.button` and `ui.buttonContains`
- `ui.listTouch`
- `ui.selectDraw` and `ui.selectHit`
- `ui.imagePath` and `ui.imageBuffer` for PNG, JPEG, WebP, and WBMP
- `ui.vectorPath` and `ui.vectorString` for SVG
- `ui.qr` for compact QR codes
- system-owned `ui.getKeyboardInput(...)` and `ui.getDateInput(...)` Promises;
  consume modal results with `.then(...)` because VM `await` is synchronous

The reusable keyboard and date-picker components allocate state on demand in
PSRAM and release it when the app exits.

## Storage and system services

The native storage API is bounded and rooted at the SD-card filesystem:

- `files.readText(path, offset?, maxBytes?)` and `files.readBinary(...)`
  return `{data, bytes, size, offset, eof}` chunks of at most 64 KiB.
- `files.write(path, data, append?)` and `files.delete(path)` return an
  `api.*` status. A first protected operation can return `api.PENDING` while
  the shell asks for permission; retry it after the prompt closes.
- `files.pick(initialPath?, extensionFilter?)` opens the system Files browser
  and returns a Promise resolving to a path or `false` on cancellation.

A small Node-like `fs` compatibility surface is also available. Methods
without the `Sync` suffix return Promises; their `Sync` counterparts return
the value directly. Promise forms are also available under `fs.promises`:

- `fs.readFile/readFileSync(path, encoding?)` return one string and are limited
  to 65,536 bytes. Supported encoding labels are `utf8`, `utf-8`, `binary`,
  and `buffer`; binary data is represented by a length-preserving string.
- `fs.writeFile/writeFileSync` and `fs.appendFile/appendFileSync` accept a path,
  string data, and optional encoding. They resolve or return an `api.*` status
  because a first protected operation may need shell permission.
- `fs.readdir/readdirSync(path, encoding?)` return up to 256 entry names.
- `fs.stat/statSync(path)` return `{size, isFile, isDirectory}`;
  `fs.existsSync(path)` checks regular files.
- `fs.unlink/unlinkSync(path)` resolve or return an `api.*` status.

Other services include app launch requests, per-app configuration, GPIO,
environment and CPU temperature snapshots, confirmed Wi-Fi scans, microphone
capture as WAV or signed 16-bit mono PCM Promises, and preloadable/replayable
audio handles. MIDI playback is always available; additional compressed audio
formats follow the firmware edition. All modal UI and captured buffers are
owned by the app API session and released when the script exits.

## Supported language

The subset includes `let`, `const`, block scope, named functions, arrows,
`async` functions and arrows, arrays, Sets, objects, computed properties,
optional chaining, nullish coalescing, template literals, common loops and
operators, Promises, minimal `async`/`await`, `Math`, JSON, Date, and basic type
constructors. Array and object destructuring works in declarations,
assignments, function parameters, classic `for`, and `for...of`. Patterns may
be nested and use array holes, defaults, object shorthand, aliases, and final
rest bindings. Spread works in arrays, objects, and calls, and final function
parameters may be rest parameters. Computed-key patterns remain unsupported;
defaults apply to leaf bindings, not an entire nested subpattern.

Basic `switch`/`case`/`default`, the comma operator, unary `delete` and `void`,
the `in` operator for objects/arrays/strings, tag-based `instanceof`, lazy
function `arguments`, and `typeof` on missing identifiers are supported.
`Object.defineProperty` accepts descriptor syntax but stores only `value`;
descriptor flags are intentionally ignored because objects have no prototype
or property-attribute layer. `arguments` is a snapshot array rather than an
alias of named parameters, and deleting a dense array index stores `undefined`
without adding a separate hole bitmap.

`throw` accepts any value. `try...catch` supports a named catch binding or
`catch { ... }`. `Error`, `TypeError`, `RangeError`, `ReferenceError`,
`SyntaxError`, `EvalError`, and `URIError` expose `name`, `message`, and
`toString()`. Thrown values cross function calls and become Promise rejections
inside async functions, Promise executors, and continuation handlers.

Modules support CommonJS `require`, `exports`, and `module.exports`, plus
side-effect/default/named/namespace imports and simple named/default exports.
Paths may be absolute or use `./` and `../`; `.js` is optional. Imports and
exports are top-level snapshots. Uncached `require()` is limited to top-level
execution, while cached modules may be required anywhere. Packages, JSON,
dynamic imports, and re-exports are unsupported.

Common collection, object, string, and number helpers are available, including
`map`, `filter`, `reduce`, `sort`, `splice`, `findIndex`, `flat`,
`Object.keys/values/entries/assign/defineProperty`, string padding and trimming,
number finite/integer checks, and `Math.sin/cos/tan/log/exp/atan2`.

Function declarations become visible only after execution reaches the
declaration; there is no separate hoisting pass. Named recursion works, but
functions do not capture caller locals. Promises currently support one
continuation branch.

Advanced object-oriented features such as classes, prototypes, and `this`,
along with closures, `try...finally`, Error stacks/custom properties, regex,
and multiple Promise branches, are unsupported. Async continuations
cannot be fully suspended, and Unicode support is limited.

## JSON parser choices

The built-in DOM parser is the default because it preserves PocketInkJS value
semantics. Firmware can select alternatives independently:

- `POCKETINKJS_DISABLE_BUILTIN_JSON_PARSER=1` removes the built-in reader.
- `PocketInkJS::Options::jsonParse` installs a replacement callback.
- `POCKETINKJS_USE_LIGHTWEIGHT_JSON_PARSER=1` selects the adapter's bounded,
  streaming parser and disables the built-in reader for that translation unit.

The lightweight parser uses bounded key/token buffers and ASCII sanitization;
use the built-in parser when exact long strings, keys, or Unicode payloads are
important.

## Memory and native bindings

All script allocations are scoped to the active app. The VM uses an adaptive
PSRAM workspace with a 160 KiB floor and reserves at least 512 KiB or 25% of
free PSRAM for fetch, UI, and system work. Limits are 96 KiB for the entry
source, 32 KiB per imported module, 48 KiB combined imports, 16 cached modules,
and 64 KiB fetch bodies. Values are compact
8-byte tagged values with 32-bit heap offsets, and the runtime uses bounded
mark/sweep GC, parser/call depth, roots, and job work. The VM control object
remains small internal-RAM state.
Unique strings assigned with standalone `+=` reuse geometrically grown PSRAM
blocks; aliased or expression-nested assignments retain immutable semantics.

Native code can temporarily borrow free VM workspace with
`runtime.leaseScratch()`:

```cpp
auto lease = runtime.leaseScratch();
PocketInkJS::ScratchAllocator allocator = lease.allocator();
void *buffer = allocator.allocate(allocator.context, bytes, 16);
if (buffer == nullptr)
  return runtime.fail(/* binding-specific error */);
decoder.render(input, buffer, bytes);
allocator.release(allocator.context, buffer);
```

Leases may make multiple aligned allocations. Blocks survive GC, released
blocks are coalesced, and remaining blocks return to the VM when the lease is
destroyed. A lease and its allocator callbacks must not outlive the
`Runtime`, cross runtime-owner cores, or be retained after the lease ends.
Allocation failure returns `nullptr` without setting a runtime error.

Register ordinary dotted names with `runtime.bind(...)` and receiver-aware
methods with `runtime.bindMethod(...)`.

Hosts enable modules with `Options::resolveModule`, `Options::readModule`, and
`evalModule(source, length, entryPath)`.

Script execution runs on an app-scoped executor task with a 40 KiB internal
stack; the task and stack are created on launch and released on exit. The
adapter permits 24 nested script calls, while VM data remains in PSRAM. This
keeps recursive parser frames away from Arduino's loop task and allows the
runtime's call-depth guard to report a normal error before the native stack
canary is reached.

## Error codes

Static interpreter and adapter failures use compact `E001`–`E255` codes.
Codes are one-indexed against `pocketinkjs_errors.txt`; adding a static error
requires running the PocketInkJS error-compaction script so existing numbers stay
stable and the 255-code limit is checked. Dynamic host and parser messages are
not compacted.

If `/misc/js_errors.txt` exists on the SD card, the app reads only the line for
the current compact error and displays its text beside the code. Without the
file, the compact code remains the fallback.

## Credits

PocketInkJS was inspired by [Elk](https://github.com/cesanta/elk), but its
implementation is a from-scratch rewrite made with GPT-5.6-sol:max. It
dramatically extends the supported features and leverages C++ rather than C.
It targets only ESP32 devices with external PSRAM.

## License

PocketInkJS is distributed as a part of PocketInkOS under CC BY-NC-SA 4.0 License
