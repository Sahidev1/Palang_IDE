# How a GTK app in C works

A short primer for reading Palang's code. Examples refer to files in `src/app/`.

## 1. GObject: classes in plain C

C has no classes, so GLib adds them with **GObject**. A "type" like
`PalangWindow` is made of:

- an **instance struct** (`struct _PalangWindow`): per-object data, whose
  first member is always the parent's instance struct (`AdwApplicationWindow
  parent_instance`). That's how "inheritance" works: a pointer to a
  `PalangWindow` is also a valid pointer to its parent struct.
- a **class struct**: one per type, holding function pointers ("virtual
  methods") that subclasses can override.

Two macros generate the boilerplate:

| Macro | Where | What it gives you |
|---|---|---|
| `G_DECLARE_FINAL_TYPE (PalangWindow, palang_window, PALANG, WINDOW, AdwApplicationWindow)` | `.h` | the typedefs, the cast macro `PALANG_WINDOW (obj)`, the type check `PALANG_IS_WINDOW (obj)`, `g_autoptr` support |
| `G_DEFINE_FINAL_TYPE (PalangWindow, palang_window, ADW_TYPE_APPLICATION_WINDOW)` | `.c` | `palang_window_get_type ()` and `palang_window_parent_class` |

You then write two functions yourself:

- `palang_window_class_init ()`: runs **once per type**. Override virtual
  methods here (`object_class->dispose = ...`).
- `palang_window_init ()`: runs **once per object**, like a constructor.

Objects are created with `g_object_new (TYPE, "property", value, ..., NULL)`.

**Chaining up**: when you override a virtual method, you usually call the
parent's version too:

```c
G_APPLICATION_CLASS (palang_application_parent_class)->startup (app);
```

**Cast macros** like `GTK_WIDGET (self)` convert between parent and child
types and, in debug builds, check at runtime that the cast is valid.

## 2. Reference counting: who frees what

Every GObject has a reference count. `g_object_new` returns an object with
count 1; `g_object_ref` adds one, `g_object_unref` removes one, and the object
is freed at 0.

API docs label every pointer you get or give:

- **transfer full**: you now own a reference and must unref/free it.
- **transfer none**: you're just borrowing it; don't free it.

Widgets are a special case: a parent widget owns its children, and a window
owns itself until it's closed. You almost never unref widgets yourself.

**Automatic cleanup** (a GCC/Clang feature that GLib wraps):

```c
g_autoptr (PalangApplication) app = palang_application_new ();  /* unref at scope end */
g_autofree char *path = g_file_get_path (file);                 /* g_free at scope end */
```

**dispose vs finalize**: `dispose` drops references to *other* objects
(it may run more than once); `finalize` frees the object's own memory (runs
once). Palang's window uses `dispose` to release its template children.

## 3. Signals: callbacks for events

Objects emit named **signals** ("clicked", "changed", "notify::dark"). You
connect a callback:

```c
g_signal_connect_object (style_manager, "notify::dark",
                         G_CALLBACK (on_dark_changed), self, 0);
```

`notify::<property>` fires when that property changes. Prefer
`g_signal_connect_object` when `self` can be destroyed before the emitter: it
disconnects automatically, so the callback never runs on freed memory.

## 4. Templates: layout in XML, behaviour in C

`data/ui/palang-window.ui` describes the window's widgets in XML. In
`class_init`, the C code:

1. points GTK at the file (`gtk_widget_class_set_template_from_resource`),
2. binds named children to struct fields (`gtk_widget_class_bind_template_child`).

In `init`, `gtk_widget_init_template ()` builds the widgets and fills in those
fields. The `.ui` files are compiled into the binary as **GResources**
(`data/palang.gresource.xml`), so they're addressed by paths like
`/io/github/sahidev1/Palang/ui/palang-window.ui`, not real files on disk.

## 5. The main loop and async code

`g_application_run ()` runs the **main loop**: it waits for events (input,
timers, network data, finished background work) and calls the matching
callbacks, one at a time, on the UI thread. **Never block it**: a slow call
freezes the whole app.

Slow operations therefore use the **async/finish pattern**:

```c
/* 1. Start the work and give a callback. Returns immediately. */
gtk_source_file_loader_load_async (loader, G_PRIORITY_DEFAULT, cancellable,
                                   NULL, NULL, NULL,
                                   on_loaded, self);

/* 2. Later, on the main loop, GLib calls you back with the result. */
static void
on_loaded (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GError) error = NULL;

  if (!gtk_source_file_loader_load_finish (GTK_SOURCE_FILE_LOADER (source),
                                           result, &error))
    g_warning ("Could not load: %s", error->message);
}
```

A `GCancellable` lets you abort the work early. Palang's AI requests (Phase 3)
use exactly this pattern.

## 6. Errors: GError

Functions that can fail take a `GError **error` as the last argument and
return `FALSE`/`NULL` on failure. Declare `g_autoptr (GError) error = NULL;`,
pass `&error`, and read `error->message`.

## 7. The application lifecycle

See the comment at the top of `palang-application.c`: **startup** (once) →
**activate** (on each launch) → main loop runs → **shutdown**. Because the app
has an application ID, a second launch doesn't start a new process; it
activates the running one, which presents its existing window.

## Building and checking for memory bugs

```sh
meson setup build        # debug build with AddressSanitizer + UBSan
ninja -C build
ninja -C build run      # runs Palang with the sanitizer settings
meson test -C build
```

If AddressSanitizer finds a bug, the program stops and prints the exact line
where the bad memory access happened and where that memory was allocated and
freed. Read the first stack trace from the top.

`tools/lsan.supp` hides leaks that LeakSanitizer reports in system libraries
(currently only fontconfig, which keeps font caches until exit). `ninja -C build
run` and `meson test` load it automatically. Running `./build/src/palang`
directly works too, but prints those harmless fontconfig reports at exit.
