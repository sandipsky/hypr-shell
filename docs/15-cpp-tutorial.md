# 15 — C++ course for hypr-shell (from zero to contributor)

This is a complete, beginner-first C++ course whose only goal is to make
you able to read and extend hypr-shell. Every lesson has three parts:

- **Learn**: the idea, explained plainly.
- **Try it**: a tiny standalone program you compile and run yourself.
- **In hypr-shell**: where the idea shows up in the real code, so it sticks.

Each lesson ends with an exercise. Do them; reading alone does not build
the muscle. Later lessons assume earlier ones. If you already know some
C++, skim until something is new, but do not skip Lessons 11 to 16, which
are where the project's own idioms live.

You need a Linux terminal with `g++` (`sudo pacman -S gcc`). Put each
example in a file, then:

```sh
g++ -std=c++20 -Wall -Wextra file.cpp -o prog && ./prog
```

`-Wall -Wextra` turns on warnings. Treat every warning as a mistake to fix;
the project builds with the same warning level.

Reference site for anything not covered here: **cppreference.com**.

---

## Lesson 0 — What a program is, and what the compiler does

### Learn

A C++ program is text. The **compiler** (`g++`) reads it, checks it for
mistakes, and turns it into machine code, an executable file you can run.
Unlike Python or JavaScript there is no interpreter at run time: the checks
happen before the program exists, and errors are reported as *compile
errors* with a file name and line number.

That checking is the whole point of C++. The compiler catches typos, wrong
types, and misuse before the bar ever starts. Learn to read its messages
(Lesson 19) and it becomes your best teacher.

Two steps happen under the hood:

1. **Compiling** each source file (`.cpp`) into an object file.
2. **Linking** all object files (and libraries like GTK) into the final
   executable.

hypr-shell has about forty source files, so it uses a build tool, **meson**,
to run those steps in the right order. For this course a single `g++`
command is enough.

### Try it

```cpp
// hello.cpp
#include <iostream>           // gives us std::cout for printing

int main() {                  // every program starts in main
    std::cout << "hello from C++\n";
    return 0;                 // 0 means "finished fine"
}
```

`#include <iostream>` pulls in a library header. `std::cout` is the standard
output stream; `<<` sends things to it. `"\n"` is a newline.

### In hypr-shell

`src/main.cpp` also has a `main()`. It hands control to GTK:

```cpp
int main(int argc, char* argv[]) {
    return hyprshell::App::create()->run(argc, argv);
}
```

`argc`/`argv` are the command-line arguments (that's how `--launcher`
arrives). Everything else happens inside GTK's `run()`.

**Exercise**: make the program print two lines, then make it print your
name using a second `<<` on the same line.

---

## Lesson 1 — Variables and types

### Learn

A variable is a named box holding a value. C++ is *statically typed*: every
box has a type fixed at compile time, and the compiler refuses to put a word
in a number box.

| Type | Holds | Example |
|------|-------|---------|
| `int` | whole number | `int count = 5;` |
| `double` | decimal number | `double opacity = 0.88;` |
| `bool` | `true` or `false` | `bool hidden = false;` |
| `char` | one character | `char c = 'x';` |
| `unsigned` | non-negative whole number | `unsigned serial = 0;` |
| `std::string` | text | `std::string name = "clock";` |

Arithmetic: `+ - * / %`. Integer division truncates: `7 / 2` is `3`; `7 % 2`
(remainder) is `1`. `7.0 / 2` is `3.5`.

`const` makes a box read-only after it's filled. Use it whenever you can:
it documents intent and stops accidental changes.

`auto` lets the compiler figure out the type from the right-hand side. Use it
when the type is obvious or annoyingly long.

```cpp
const int max_popups = 5;
auto percent = 42;          // int
auto fraction = 0.5;        // double
```

### Try it

```cpp
#include <iostream>
#include <string>

int main() {
    int level = 73;
    double fraction = level / 100.0;   // 100.0 makes it decimal division
    bool charging = true;
    std::string label = "Battery";
    const int deciles = 10;

    std::cout << label << ": " << level << "% (" << fraction << ")\n";
    std::cout << "charging: " << charging << "\n";      // prints 1 for true
    std::cout << "decile index: " << (level + 9) / deciles - 1 << "\n";
    return 0;
}
```

Change `level` to `100` and to `3`; check the decile index stays in 0..9.

### In hypr-shell

GLib adds fixed-size number aliases you'll meet in DBus code: `gint64`,
`guint32`, `gsize`, `gboolean`. They are just integers with a guaranteed
size. From `src/bar/modules/battery.cpp`:

```cpp
auto pct = upower.percentage();                                     // double
auto idx = std::clamp(static_cast<int>(std::ceil(pct / 10.0)) - 1, 0, 9);
```

`static_cast<int>(...)` converts a double to an int on purpose (Lesson 7).
`std::clamp(x, lo, hi)` limits a value to a range.

**Exercise**: write a program that stores a volume from 0.0 to 1.0 and
prints it as a whole percent (hint: multiply, then `static_cast<int>`).

---

## Lesson 2 — Strings

### Learn

`std::string` (from `#include <string>`) is text of any length. You can join
with `+`, compare with `==`, and ask `.size()` and `.empty()`.

Finding and slicing are the two operations the project uses constantly:

```cpp
std::string s = "firefox,Mozilla Firefox — Home, sweet home";
auto comma = s.find(',');               // position of the first ',' or std::string::npos
std::string klass = s.substr(0, comma); // from 0, length comma → "firefox"
std::string title = s.substr(comma + 1);// from comma+1 to the end
```

`std::string::npos` means "not found". Always check for it before using the
position.

Two other string types you'll see: `const char*`, the old C-style string (a
`"literal"` is one of these; C libraries want it, get it from a
`std::string` with `.c_str()`), and `Glib::ustring`, GTK's UTF-8 string,
which converts to and from `std::string` automatically.

### Try it

```cpp
#include <iostream>
#include <string>

int main() {
    std::string event = "activewindow>>kitty,~/projects — fish";
    auto sep = event.find(">>");
    std::string name = event.substr(0, sep);
    std::string data = event.substr(sep + 2);

    auto comma = data.find(',');                 // FIRST comma only
    std::string klass = data.substr(0, comma);
    std::string title = data.substr(comma + 1);  // may itself contain commas

    std::cout << "event=" << name << " class=" << klass << " title=" << title << "\n";
    std::cout << "starts with 'ok'? " << (event.rfind("ok", 0) == 0) << "\n";
}
```

### In hypr-shell

This is exactly `Hyprland::handle_event_line()` plus the `activewindow`
parsing in `active_window.cpp`. Titles contain commas, which is why the code
splits on the *first* one. The odd-looking `s.rfind("ok", 0) == 0` idiom
means "starts with ok" (in C++20 you may also write `s.starts_with("ok")`).

**Exercise**: given `"3,workspace name with, commas"`, split into the number
part and the name part, and print both.

---

## Lesson 3 — Making decisions

### Learn

```cpp
if (condition) { ... } else if (other) { ... } else { ... }
```

Comparisons: `== != < <= > >=`. Combine with `&&` (and), `||` (or), `!`
(not).

The conditional expression `a ? b : c` means "if a then b else c" and is
handy for choosing one of two values in a single line.

`switch` picks a branch by value. Each `case` should end in `break`, or
execution falls into the next case.

**Enumerations** name a fixed set of choices. Always prefer `enum class`:
its values are scoped (`BarPosition::Top`) and don't silently turn into
numbers.

```cpp
enum class BarPosition { Top, Bottom, Left, Right };
```

### Try it

```cpp
#include <iostream>

enum class Urgency { Low, Normal, Critical };

int duration_seconds(Urgency u) {
    switch (u) {
    case Urgency::Low:      return 3;
    case Urgency::Normal:   return 8;
    case Urgency::Critical: return 15;
    }
    return 8; // unreachable, but keeps the compiler quiet
}

int main() {
    double volume = 0.35;
    bool muted = false;

    const char* icon = muted ? "volume-off" : volume < 0.005 ? "volume-0"
                             : volume <= 0.5 ? "volume-1" : "volume-2";
    std::cout << icon << "\n";
    std::cout << duration_seconds(Urgency::Critical) << "s\n";
}
```

Try `muted = true`, then `volume = 0.9`.

### In hypr-shell

`Volume::update()` picks the glyph with exactly this if-chain. `Config` is
full of `enum class` (`BarPosition`, `BarVisibility`, `WorkspacesMode`,
`Notifications::Location`) and every module `switch`es over
`cfg.bar_position()` to decide which side a popover opens on. The compiler
warns if a `switch` over an `enum class` misses a value, so listing every
case is a free safety net when you add a new one.

**Exercise**: write `wifi_bucket(int strength)` returning 0..4 for the
thresholds ≥80, ≥60, ≥35, ≥15, below, as `network.cpp` does.

---

## Lesson 4 — Repeating: loops and vectors

### Learn

`std::vector<T>` (from `#include <vector>`) is a resizable list of `T`.
`push_back` appends, `size()` counts, `[i]` indexes from 0, `clear()`
empties, `empty()` tests, `front()`/`back()` are the ends.

The **range-based for** loop visits every element:

```cpp
for (const auto& item : items) { ... }
```

`const auto&` means "a read-only reference to each element, no copy". Use it
by default. Drop the `const` if you need to modify elements in place.

Classic loops still exist: `for (int i = 0; i < n; ++i)` and `while (cond)`.

### Try it

```cpp
#include <iostream>
#include <string>
#include <vector>

struct Workspace {      // a bundle of fields (more in Lesson 8)
    int id;
    std::string name;
    int windows;
};

int main() {
    std::vector<Workspace> all = {{1, "1", 2}, {3, "web", 0}, {7, "7", 1}};
    const int fixed_count = 5;

    // Fixed mode: show 1..fixed_count, placeholders for missing ids
    std::vector<Workspace> shown;
    for (int n = 1; n <= fixed_count; ++n) {
        bool found = false;
        for (const auto& ws : all) {
            if (ws.id == n) { shown.push_back(ws); found = true; break; }
        }
        if (!found) shown.push_back({n, std::to_string(n), 0});
    }
    for (const auto& ws : all)
        if (ws.id > fixed_count) shown.push_back(ws);   // keep real ones beyond

    for (const auto& ws : shown)
        std::cout << ws.name << (ws.windows > 0 ? "*" : "") << " ";
    std::cout << "\n";
}
```

Expected: `1* 2 web 4 5 7* `.

### In hypr-shell

That is `Workspaces::rebuild()` almost line for line (it uses
`std::find_if`, which you'll meet in Lesson 6, instead of the inner loop).
Vectors are everywhere: `Config`'s layout sections, `Bluez::devices()`,
`NotificationService::history()`.

**Exercise**: given a vector of ints, print only the ones that are ≥ 0 (the
project drops negative workspace ids, which are scratchpads).

---

## Lesson 5 — Functions

### Learn

A function packages code you can call by name.

```cpp
int add(int a, int b) { return a + b; }
void greet(const std::string& who) { std::cout << "hi " << who << "\n"; }
```

`void` means "returns nothing".

**How arguments are passed matters:**

- `int n` — **by value**: the function gets a copy. Fine for small types.
- `const std::string& s` — **by const reference**: no copy, the function
  can read but not change the caller's variable. Use this for strings,
  vectors, and any object you only read.
- `std::string& s` — **by reference**: the function can change the caller's
  variable. Rare in this codebase; getters return const references instead.

**Overloading**: two functions with the same name but different parameter
types. The compiler picks by what you pass.

**Default arguments** let callers omit trailing parameters.

### Try it

```cpp
#include <iostream>
#include <string>

std::string focus_lua(int id) {
    return "hl.dsp.focus({ workspace = " + std::to_string(id) + " })";
}
std::string focus_lua(const std::string& selector) {           // overload
    return "hl.dsp.focus({ workspace = \"" + selector + "\" })";
}
void connect(const std::string& ssid, const std::string& password = "") {
    std::cout << "connect " << ssid << (password.empty() ? " (open)" : " (secured)") << "\n";
}

int main() {
    std::cout << focus_lua(3) << "\n";
    std::cout << focus_lua("e+1") << "\n";
    connect("CoffeeShop");
    connect("Home", "hunter2");
}
```

### In hypr-shell

`Hyprland::focus_workspace(int)` and `focus_workspace(const std::string&)`
are these overloads. `NetworkManager::wifi_connect(ssid, password = "")` is
the default argument. Look at any header in `src/services/`: nearly every
parameter that is a string or vector is `const T&`.

**Exercise**: write `vague_duration(long seconds)` returning `"1h 5m"`,
`"3m"`, `"45s"` (only non-zero parts), then compare with the lambda of the
same name in `battery.cpp`.

---

## Lesson 6 — The standard library toolbox

### Learn

You've met `vector` and `string`. Three more containers appear in the code:

```cpp
#include <map>      // sorted key → value dictionary
std::map<std::string, bool> modules;
modules["clock"] = false;                    // insert or overwrite
auto it = modules.find("clock");             // an iterator, or modules.end() if absent
if (it != modules.end()) std::cout << it->first << "=" << it->second;

#include <array>    // fixed-size array, size known at compile time
std::array<int, 3> counts{};                 // three zeros

#include <set>      // sorted set of unique values
std::set<std::string> saved; saved.insert("Home"); saved.count("Home") == 1
```

`std::pair<A, B>` holds two values (`.first`, `.second`). **Structured
bindings** unpack pairs and small structs into named variables:

```cpp
for (const auto& [name, enabled] : modules) ...
```

**Algorithms** (`#include <algorithm>`) work on any container via
`begin()`/`end()`:

| Call | Meaning |
|------|---------|
| `std::find(v.begin(), v.end(), x) != v.end()` | contains `x`? |
| `std::find_if(v.begin(), v.end(), pred)` | first element where `pred` is true |
| `std::sort(v.begin(), v.end(), cmp)` | sort with a comparison |
| `std::clamp(x, lo, hi)` | limit to a range |
| `std::upper_bound / lower_bound` | binary search in a sorted vector |
| `std::min`, `std::max`, `std::swap` | as named |

`pred` and `cmp` are usually lambdas (Lesson 12); for now read them as
"a small function written inline".

`#include <cmath>`: `std::lround`, `std::ceil`, `std::floor`, `std::abs`,
`std::pow`.

### Try it

```cpp
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

int main() {
    std::map<std::string, bool> modules = {{"clock", true}, {"battery", false}};
    for (const auto& [name, enabled] : modules)
        std::cout << name << " -> " << (enabled ? "on" : "off") << "\n";

    // "absent means enabled"
    auto enabled = [&](const std::string& n) {
        auto it = modules.find(n);
        return it == modules.end() ? true : it->second;
    };
    std::cout << "volume: " << enabled("volume") << "\n";   // 1

    std::vector<int> ids = {7, 1, 3};
    std::sort(ids.begin(), ids.end());
    int active = 3;
    auto next = std::upper_bound(ids.begin(), ids.end(), active); // first > 3
    std::cout << "next after 3: " << (next != ids.end() ? *next : ids.front()) << "\n";
}
```

### In hypr-shell

`Config::module_enabled()` is the map lookup with "absent = true".
`Workspaces::step()` uses `upper_bound`/`lower_bound` over the sorted
shown ids to scroll to the next/previous workspace, wrapping to
`front()`/`back()` when configured. `for (const auto& [key, section] :
kKnownModules)` iterates a table of pairs.

**Exercise**: given `std::vector<std::string> layout = {"clock", "clock",
"bogus", "battery"}` and a set of known names, build a new vector with
unknown names dropped and duplicates keeping their first position. That is
the layout resolver from `config.cpp`.

---

## Lesson 7 — Type conversions and casting

### Learn

C++ converts between number types implicitly, sometimes with warnings
(`double` → `int` loses the fraction; signed ↔ unsigned comparisons). Say
what you mean with a cast:

- `static_cast<int>(3.7)` — the normal, checked-at-compile-time cast between
  related types. Use this.
- `dynamic_cast<Derived*>(base_ptr)` — runtime check when going *down* a
  class hierarchy (Lesson 11); gives `nullptr` if the object isn't that type.
- `const_cast<char*>(p)` — removes `const`. Only to satisfy a C API that
  promises not to write.
- `(int)x` — old C-style cast. Legal but hides which of the above you meant;
  prefer `static_cast` in new code.
- `GTK_WINDOW(ptr)`, `ADW_COMBO_ROW(ptr)` — GObject macros that cast C
  pointers and check the type at run time.

Number → text: `std::to_string(42)`. Never use it for decimals that go into
CSS or Lua: in some locales it produces `0,88`. The project uses GLib's
`g_ascii_dtostr` for that (Lesson 16).

### Try it

```cpp
#include <cmath>
#include <iostream>

int main() {
    double volume = 0.678;
    int pct_truncated = static_cast<int>(volume * 100);        // 67
    int pct_rounded = static_cast<int>(std::lround(volume * 100)); // 68
    std::cout << pct_truncated << " " << pct_rounded << "\n";

    unsigned u = 3;
    int i = -1;
    // std::cout << (i < u);   // WARNING: -1 becomes a huge unsigned. Don't mix.
    std::cout << (i < static_cast<int>(u)) << "\n";
}
```

### In hypr-shell

`static_cast<int>(std::lround(pulse.volume() * 100.0))` is how percents are
shown. `static_cast<std::size_t>(section)` turns an `enum class` into an
array index in `Config::bar_layout()`. `static_cast<Settings*>(data)`
recovers a typed pointer from a C `gpointer` in the settings app.

**Exercise**: write `decile_index(double percent)` returning 0..9 exactly as
`battery.cpp` does, and test it with 0, 1, 10, 11, 99, 100.

---

## Lesson 8 — Structs and classes

### Learn

A `struct` bundles related fields into one type:

```cpp
struct WifiNetwork {
    std::string ssid;
    std::string security;   // "" = open
    int signal = 0;         // default value
    bool saved = false;
};
WifiNetwork n{"Home", "WPA2", 80, true};
n.signal = 75;
```

A `class` is the same thing plus **behaviour** (member functions, also
called methods) and **access control**: `public` members are usable from
anywhere, `private` ones only from inside the class. By convention the
project uses `struct` for plain data and `class` when there is behaviour;
private member variables end in an underscore (`volume_`).

The **constructor** runs when an object is created and sets it up. The
**destructor** (`~Name()`) runs when it is destroyed and cleans up. Inside a
method, `this` points to the current object; you rarely need to write it.

A method marked `const` promises not to change the object. Getters are
`const`. Defining a method's body inside the class makes it "inline"; short
getters are written that way.

### Try it

```cpp
#include <iostream>
#include <string>

class Counter {
public:
    Counter(const std::string& name) : name_(name) {        // initialiser list
        std::cout << name_ << " created\n";
    }
    ~Counter() { std::cout << name_ << " destroyed at " << value_ << "\n"; }

    void increment() { ++value_; }
    int value() const { return value_; }                    // const getter

private:
    std::string name_;
    int value_ = 0;                                         // default member initialiser
};

int main() {
    Counter c("clicks");
    c.increment();
    c.increment();
    std::cout << c.value() << "\n";
    // c.value_ = 5;   // ERROR: private
}   // c goes out of scope here → destructor prints
```

The part after the colon in the constructor, `: name_(name)`, is the
**member initialiser list**: it initialises members *before* the body runs.
Members are initialised in the order they are declared in the class, not
the order in the list.

### In hypr-shell

Every service and every module is a class. `Pulse`:

```cpp
class Pulse {
public:
    bool available() const { return available_; }
    double volume() const { return volume_; }
    void set_volume(double volume);
private:
    Pulse();
    bool available_ = false;
    double volume_ = 0.0;
};
```

Constructors that "do work" are normal here: a module's constructor adds
CSS classes, connects signals, and starts I/O. Destructors disconnect
timers and unparent popovers (Lesson 10 explains why that matters).

**Exercise**: write a `Battery` class with `percentage_` and `plugged_`,
a constructor taking both, `const` getters, and a `tooltip()` method
returning `"Battery level: 73%\nPlugged in"` or `"...\nDischarging"`.

---

## Lesson 9 — Headers, sources, namespaces, and the build

### Learn

Real programs split each class into two files:

- `name.hpp` — the **declaration**: what the class looks like. Other files
  `#include` this.
- `name.cpp` — the **definition**: the bodies of the methods.

```cpp
// counter.hpp
#pragma once             // guard: include at most once per compilation
#include <string>

namespace hyprshell {

class Counter {
public:
    explicit Counter(const std::string& name);
    void increment();
    int value() const { return value_; }   // tiny: define inline
private:
    std::string name_;
    int value_ = 0;
};

} // namespace hyprshell
```

```cpp
// counter.cpp
#include "counter.hpp"

namespace hyprshell {

Counter::Counter(const std::string& name) : name_(name) {}
void Counter::increment() { ++value_; }

} // namespace hyprshell
```

`Counter::increment` means "the `increment` that belongs to `Counter`".

A **namespace** groups names to avoid clashes. All project code is inside
`namespace hyprshell`; library code is in `Gtk::`, `Glib::`, `Gio::`,
`std::`. An **anonymous namespace** (`namespace { ... }`) at the top of a
`.cpp` makes helpers and constants private to that file.

The compiler compiles each `.cpp` separately; the **linker** then joins the
results. If a `.cpp` is not compiled, its functions are missing and you get
`undefined reference to ...` at link time. That is why hypr-shell lists
every `.cpp` in `meson.build`.

`#include <...>` is for system and library headers; `#include "..."` is for
your own, with paths relative to the include root (`src/` in this project).

### Try it

Split the Lesson 8 `Counter` into `counter.hpp` / `counter.cpp` and a
`main.cpp` that includes the header. Compile all sources together:

```sh
g++ -std=c++20 -Wall -Wextra main.cpp counter.cpp -o prog && ./prog
```

Now compile *without* `counter.cpp` and read the linker error; that is the
message you'll see when you forget to add a file to `meson.build`.

### In hypr-shell

Open `src/bar/modules/clock.hpp` and `clock.cpp` side by side: the header
declares `Clock`, the source defines `Clock::Clock()`, `Clock::update()`,
`Clock::schedule_next_minute()`. `meson.build`'s `sources = files(...)` list
is the build's roll call.

**Exercise**: add a `reset()` method to `Counter`, declared in the header,
defined in the source. Forget the definition on purpose once to see the
error.

---

## Lesson 10 — Memory and ownership

### Learn

This is the lesson that separates C++ from garbage-collected languages, and
the one that matters most for not crashing GTK.

**Every object has exactly one owner, and the owner's lifetime decides the
object's lifetime.** There is no garbage collector. Instead:

**Stack values (the default).** A local variable or a member variable is
created where it's declared and destroyed automatically when its scope or
owner ends. Prefer this. Modules are members of `Bar`; widgets are members
of modules. Nothing to free, nothing to forget.

**References (`T&`).** An alias for an existing object. Cannot be null,
cannot be re-pointed. Use for function parameters and for
`auto& cfg = Config::get();`.

**Pointers (`T*`).** An address. May be `nullptr`. Access members with
`->`. In hypr-shell a raw pointer is always **non-owning**: someone else
keeps the object alive. `BatteryPanel* panel_` points at a widget GTK owns.

**Smart pointers own things for you:**

- `std::unique_ptr<T>` — one owner; deletes the object when the pointer
  dies. Create with `std::make_unique<T>(...)`. Cannot be copied, can be
  moved.
- `std::shared_ptr<T>` — shared ownership by reference count; the object
  dies when the last copy dies. `std::make_shared<T>(...)`.
- `Glib::RefPtr<T>` — GLib's version of `shared_ptr` for its own objects
  (`Gio::File`, `Gio::DBus::Proxy`, controllers). Created with
  `T::create(...)`.

**GTK's own rule.** Widgets created with `Gtk::make_managed<T>(...)` are
owned by the container you add them to; keep only a raw pointer. Widgets
that are value members are owned by the enclosing object.

Bare `new` and `delete` are essentially banned in the project; the two
`new` calls that exist immediately hand ownership to a smart pointer or a
GObject destroy callback.

**RAII** (Resource Acquisition Is Initialisation) is the name of the
pattern: acquire in the constructor, release in the destructor, and cleanup
becomes automatic and exception-safe.

### Try it

```cpp
#include <iostream>
#include <memory>
#include <string>

struct Panel {
    std::string name;
    Panel(std::string n) : name(std::move(n)) { std::cout << "open " << name << "\n"; }
    ~Panel() { std::cout << "close " << name << "\n"; }
};

void show(const Panel& p) { std::cout << "showing " << p.name << "\n"; }  // reference: no copy

int main() {
    Panel stack_panel("calendar");                 // stack: destroyed at end of main
    show(stack_panel);

    auto owned = std::make_unique<Panel>("battery"); // unique owner
    Panel* view = owned.get();                       // non-owning pointer into it
    show(*view);                                     // * dereferences the pointer
    owned.reset();                                   // destroys "battery" now
    // show(*view);   // BUG: dangling pointer — view points at a dead object

    {
        auto a = std::make_shared<Panel>("audio");
        auto b = a;                                  // shared: count = 2
        std::cout << "refs: " << a.use_count() << "\n";
    }                                                // both die → "close audio"
    std::cout << "end of main\n";
}
```

Watch the order of "open"/"close" lines. Uncomment the dangling `show` to
see undefined behaviour (it may even seem to work: that's the danger).

### In hypr-shell

- `std::unique_ptr<Bar> bar_` in `App`: the app owns the bar.
- `Gtk::make_managed<Gtk::Button>(entry.name)` in `Workspaces::rebuild()`:
  the box owns the buttons; `remove()` destroys them.
- `auto payload = std::make_shared<std::string>(command);` in
  `Hyprland::request()`: keeps the bytes alive until the async write is
  done (Lesson 15 explains why).
- `Glib::RefPtr<Gio::FileMonitor> monitor_` as a member of `Config`: if the
  RefPtr were a local variable, the monitor would die immediately and hot
  reload would silently stop working. Store RefPtrs you need to keep.
- Destructors: `Clock::~Clock() { popover_.unparent(); }` and
  `Bar::~Bar()` disconnecting timers. A timer that fires after its widget
  is gone is a crash.

Three bugs to recognise on sight:

1. **Dangling pointer/reference**: using an object after its owner freed it.
2. **Callback outliving its object**: a lambda captured `this`, the object
   died, the callback ran later.
3. **Iterator invalidation**: modifying a vector while iterating it. Collect
   changes first, apply after.

**Exercise**: write a program with a `std::vector<std::unique_ptr<Panel>>`,
push three panels, erase the middle one, and observe which destructor runs
and when.

---

## Lesson 11 — Inheritance and the widget hierarchy

### Learn

`class Clock : public Gtk::Label` means "a `Clock` **is a** `Gtk::Label`,
plus extras". Everything a label can do, a clock can do, and any code that
accepts a `Gtk::Widget*` or `Gtk::Widget&` accepts a `Clock`.

A base class can declare `virtual` methods that derived classes replace.
Mark your replacement with `override`; the compiler then errors if you
misspelled the name or got the signature wrong. `Gtk::Application::on_activate`
is one such method.

Destructors in the GTK hierarchy are virtual, so a derived class writes
`~Clock() override;`.

`= delete` on the copy constructor and copy assignment forbids copying.
Services and windows must not be duplicated; deleting the copies makes the
compiler enforce that.

`dynamic_cast<const Gtk::Popover*>(widget)` asks at run time "is this
widget actually a popover?" and returns `nullptr` if not.

### Try it

```cpp
#include <iostream>
#include <memory>
#include <vector>

class Widget {
public:
    virtual ~Widget() = default;
    virtual void draw() const { std::cout << "generic widget\n"; }
};

class Label : public Widget {
public:
    void draw() const override { std::cout << "label\n"; }
};

class Clock : public Label {
public:
    void draw() const override { std::cout << "clock showing 12:00\n"; }
    void tick() { std::cout << "tick\n"; }
};

int main() {
    std::vector<std::unique_ptr<Widget>> bar;
    bar.push_back(std::make_unique<Label>());
    bar.push_back(std::make_unique<Clock>());
    for (const auto& w : bar) w->draw();          // picks the most-derived draw()

    for (const auto& w : bar)
        if (auto* clock = dynamic_cast<Clock*>(w.get())) clock->tick();  // only the clock
}
```

### In hypr-shell

`Bar::module_widget(name)` returns `Gtk::Widget*` for modules of six
different classes; `apply_config()` appends them to boxes without caring
which is which. `Bar::popover_open()` walks the widget tree using
`dynamic_cast<const Gtk::Popover*>` to find any open popover.
`Config(const Config&) = delete;` appears in every service.

**Exercise**: add a `Button : public Widget` with a `click()` method to the
example, store one in the vector, and call `click()` only on buttons.

---

## Lesson 12 — Lambdas: functions written inline

### Learn

A **lambda** is a small unnamed function you write right where you need it.
Almost every callback in hypr-shell is one.

```cpp
[captures](parameters) { body }
```

The **capture list** says which outside variables the body may use:

| Capture | Meaning |
|---------|---------|
| `[]` | nothing from outside |
| `[this]` | the current object, so the body can use its members |
| `[x]` | a **copy** of `x`, taken now |
| `[&x]` | a **reference** to `x`; dangerous if the lambda runs after `x` is gone |
| `[id = entry.id]` | *init-capture*: create a new variable from an expression |
| `[=]` / `[&]` | everything by copy / by reference — avoid; be explicit |

Parameters you don't use can be left unnamed: `(int, double, double)`.

Lambdas have unnameable types. To store one in a variable or pass it
around generically, use `std::function<ReturnType(Args...)>`.

A lambda with **no captures** can be converted to a plain C function
pointer; write `+[](...) { ... }` to force that when a C API demands it.

### Try it

```cpp
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct Button {
    std::string label;
    std::function<void()> on_click;      // any callable taking nothing, returning nothing
    void click() const { if (on_click) on_click(); }
};

int main() {
    int total = 0;
    std::vector<Button> buttons;
    for (int id : {1, 2, 3}) {
        buttons.push_back({std::to_string(id),
                           [id, &total] {                 // copy id, reference total
                               total += id;
                               std::cout << "focus workspace " << id << "\n";
                           }});
    }
    for (const auto& b : buttons) b.click();
    std::cout << "total " << total << "\n";

    auto starts_with_ok = [](const std::string& s) { return s.rfind("ok", 0) == 0; };
    std::cout << starts_with_ok("ok done") << starts_with_ok("error") << "\n";
}
```

Why `[id, ...]` by copy? `id` is the loop variable; it changes on the next
iteration and dies after the loop, but the lambda runs later. Copying at
capture time freezes the value each button needs.

### In hypr-shell

```cpp
button->signal_clicked().connect([id = entry.id] {
    Hyprland::get().focus_workspace(id);
});
```

is the workspace button. `using ReplyHandler = std::function<void(const
std::string&)>;` is how `Hyprland::request` accepts any callback. In the
settings app, `G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) { ... })` is a
capture-less lambda handed to C.

**Exercise**: write a `std::vector<std::function<int(int)>>` holding
"double it", "add ten", and "negate" lambdas, then apply all three to 5.

---

## Lesson 13 — Errors and exceptions

### Learn

When something fails deep inside a library, C++ can **throw** an exception.
It unwinds the call stack until a `try { } catch (...) { }` handles it. If
nothing catches it, the program aborts.

```cpp
try {
    risky();
} catch (const SomeError& e) {
    std::cerr << e.what() << "\n";
}
```

Catch by `const&`. Most exception types derive from `std::exception`, so
`catch (const std::exception& e)` is a good final net. glibmm throws
`Glib::Error`; nlohmann-json throws `nlohmann::json::exception` (a
`std::exception`).

Rules the project follows:

- Wrap every `*_finish()` of an async call and every parse of external data
  in `try/catch`. An exception escaping a GTK callback kills the shell.
- Prefer APIs that don't throw when they exist: `json::parse(text, nullptr,
  false)` returns a "discarded" value instead of throwing; `j.value("key",
  default)` returns the default if the key is missing.
- Don't use exceptions for expected situations ("no battery in this
  machine"). Return a flag (`available() == false`) and log at debug level.

### Try it

```cpp
#include <iostream>
#include <stdexcept>
#include <string>

int parse_percent(const std::string& text) {
    int value = std::stoi(text);                 // throws std::invalid_argument on junk
    if (value < 0 || value > 100)
        throw std::out_of_range("percent must be 0..100, got " + std::to_string(value));
    return value;
}

int main() {
    for (const auto* input : {"42", "abc", "150"}) {
        try {
            std::cout << input << " -> " << parse_percent(input) << "\n";
        } catch (const std::out_of_range& e) {
            std::cout << input << " -> range error: " << e.what() << "\n";
        } catch (const std::exception& e) {
            std::cout << input << " -> error: " << e.what() << "\n";
        }
    }
}
```

### In hypr-shell

```cpp
hypr.request("j/activeworkspace", [this, serial](const std::string& reply) {
    bool empty = false;
    try {
        empty = nlohmann::json::parse(reply).value("windows", 0) == 0;
    } catch (const std::exception&) {
        return;                       // malformed reply: ignore
    }
    ...
});
```

and every DBus proxy creation:

```cpp
try {
    proxy_ = Gio::DBus::Proxy::create_for_bus_finish(result);
} catch (const Glib::Error& e) {
    g_debug("UPower unavailable: %s", e.what());
    return;                            // available_ stays false
}
```

**Exercise**: extend `parse_percent` to accept `"73%"` by stripping a
trailing `%`, and make sure `"%"` alone is handled without crashing.

---

## Lesson 14 — Templates: using generic code

### Learn

A **template** is a type or function with a blank you fill with a type.
You've been using them: `std::vector<int>` is the vector template with
`int` filled in. In hypr-shell you *use* templates constantly and almost
never write one.

```cpp
std::vector<std::string>                          // vector of strings
std::map<std::string, bool>                       // map from string to bool
Glib::RefPtr<Gio::DBus::Proxy>                    // ref-pointer to a proxy
sigc::signal<void(const std::string&, int)>       // signal carrying a string and an int
Glib::Variant<Glib::ustring>                      // DBus variant holding a string
Gtk::make_managed<Gtk::Button>("1")               // create a managed Button
std::make_unique<Bar>()                           // create a uniquely owned Bar
static_cast<int>(x)                               // yes, casts are templates too
```

`using Name = LongType;` makes a short alias: `using json = nlohmann::json;`.

Template **error messages** are long. Find the first line that names *your*
file; everything above it is the library unwinding.

### Try it

```cpp
#include <iostream>
#include <string>
#include <vector>

// A tiny template of our own, to see the mechanism once.
template <typename T>
T clamp_to(T value, T lo, T hi) {
    return value < lo ? lo : value > hi ? hi : value;
}

template <typename T>
void print_all(const std::vector<T>& items) {
    for (const auto& item : items) std::cout << item << " ";
    std::cout << "\n";
}

int main() {
    std::cout << clamp_to(150, 0, 100) << " " << clamp_to(0.5, 0.0, 1.0) << "\n";
    print_all(std::vector<int>{1, 2, 3});
    print_all(std::vector<std::string>{"left", "center", "right"});
}
```

`clamp_to(150, 0, 100)` fills `T = int`; the second call fills `T = double`.
The standard library's `std::clamp` is exactly this.

### In hypr-shell

Reading a DBus property is the densest template code you'll meet:

```cpp
Glib::VariantBase value;
proxy_->get_cached_property(value, "Percentage");
if (value.gobj() != nullptr)
    percentage_ = Glib::VariantBase::cast_dynamic<Glib::Variant<double>>(value).get();
```

Read it inside-out: `Glib::Variant<double>` is "a variant holding a
double"; `cast_dynamic<That>(value)` checks and converts; `.get()` extracts
the number. The `T` must match the DBus type exactly (`bool` for `b`,
`guint32` for `u`, `gint64` for `x`, `double` for `d`, `Glib::ustring` for
`s`) or it throws `std::bad_cast` at run time.

**Exercise**: write `template <typename T> T first_or(const std::vector<T>&
v, T fallback)` returning the first element or the fallback if empty.

---

## Lesson 15 — Callbacks, the event loop, and async thinking

### Learn

hypr-shell has **one thread** and an **event loop** (GTK's main loop). The
loop waits for something to happen (a click, a timer, bytes arriving on a
socket, a DBus message), runs the callback registered for it, and goes back
to waiting. Your code only ever runs inside such callbacks.

Two consequences shape the entire codebase:

1. **Never block.** If a callback waits 300 ms for a socket reply, the
   whole bar freezes for 300 ms. So every I/O call is split in two: a
   `start_async(..., callback)` that returns immediately, and a
   `finish(result)` you call inside the callback when the data is ready.
2. **Callbacks run later, in a different context.** Anything they need must
   still exist then. Capture by copy or by smart pointer, not by reference
   to a local.

The pattern:

```cpp
void Service::refresh() {
    auto file = Gio::File::create_for_path(path_);
    file->load_contents_async([this, file](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            char* data = nullptr; gsize len = 0;
            file->load_contents_finish(result, data, len);   // may throw
            // ... use data ...
            g_free(data);
        } catch (const Glib::Error& e) {
            g_warning("read failed: %s", e.what());
        }
    });
    // execution continues here immediately; the lambda runs later
}
```

`[this, file]` copies the `RefPtr`, keeping the file object alive until
the callback finishes. `this` is safe because services are singletons that
live forever (Lesson 16).

**Signals** are the same idea for in-process events: a service changes,
emits, and every connected callback runs. Under the hood a signal is just a
list of `std::function`s.

**The serial guard.** If refreshes can overlap, an *older* reply may arrive
*after* a newer one and overwrite fresh state. Fix: number each request and
ignore replies whose number isn't the latest.

### Try it

We can simulate an event loop with a queue of callbacks:

```cpp
#include <functional>
#include <iostream>
#include <queue>
#include <string>

std::queue<std::function<void()>> loop;              // pretend main loop

struct Workspaces {
    unsigned serial_ = 0;
    std::string shown_;

    void refresh(const std::string& fake_reply, int delay_slots) {
        const auto serial = ++serial_;
        // "send request"; the reply arrives after delay_slots loop iterations
        std::function<void()> deliver = [this, serial, fake_reply] {
            if (serial != serial_) { std::cout << "ignored stale " << fake_reply << "\n"; return; }
            shown_ = fake_reply;
            std::cout << "applied " << fake_reply << "\n";
        };
        for (int i = 0; i < delay_slots; ++i) {
            auto inner = deliver;
            deliver = [inner] { loop.push(inner); };
        }
        loop.push(deliver);
    }
};

int main() {
    Workspaces ws;
    ws.refresh("workspaces v1", 2);   // slow reply
    ws.refresh("workspaces v2", 0);   // fast reply, sent later
    while (!loop.empty()) { auto cb = loop.front(); loop.pop(); cb(); }
    std::cout << "final: " << ws.shown_ << "\n";
}
```

Without the serial check, `v1` would overwrite `v2`. Remove the `if` line
to see it.

### In hypr-shell

`Hyprland::request()` is the real async chain: `connect_async` →
`write_all_async` → recursive `read_bytes_async` until EOF → call the
handler. `Workspaces::refresh()` guards with `refresh_serial_`. Timers are
callbacks too: `Glib::signal_timeout().connect(slot, ms)` returns a
`sigc::connection` you must `disconnect()` before the target dies.

**Exercise**: in the simulation, add a `sigc`-style `changed` list of
callbacks that `refresh` notifies after applying, and register two
listeners.

---

## Lesson 16 — Idioms of this codebase

You now know enough C++ to read the project. These are the recurring
patterns you'll see and should copy.

### The singleton service

```cpp
class Pulse {
public:
    static Pulse& get() {
        static Pulse instance;      // built on first call, lives until exit
        return instance;
    }
    Pulse(const Pulse&) = delete;
    Pulse& operator=(const Pulse&) = delete;
private:
    Pulse();                        // only get() can construct
};
```

A function-local `static` is initialised once. Every module calls
`Pulse::get()` and gets the same object, and `[this]` captures in service
callbacks are safe because the service never dies.

### State + signal

Services keep plain fields, expose `const` getters, and emit one
`signal_changed()` after every update. Consumers connect once and call
`update()` which reads the getters. Nothing is pushed through the signal
itself.

### Optimistic update

```cpp
void PowerProfiles::set_profile(const std::string& profile) {
    profile_ = profile;   // update locally first
    changed_.emit();      // UI reflects it immediately
    /* then send the DBus request; the daemon's own notification confirms */
}
```

Prevents sliders from bouncing back while the round trip is in flight. The
"pending target" variant (`pending_power_` in `Bluez`) also ignores stale
reads until the daemon confirms or a timer expires.

### The `updating_` / `loading` guard

Setting a slider's value programmatically fires the same signal as a user
drag. A boolean flag suppresses the write-back:

```cpp
updating_ = true;
scale.set_value(pct);
updating_ = false;
// handler: if (!updating_) Pulse::get().set_volume(...);
```

### Table-driven code

Constant arrays of structs drive loops instead of copy-pasted blocks:
`kKnownModules`, `kModules`, `bat_rows[]`. Adding a row adds a feature.

### RAII for cleanup on every exit path

`Config::load()` has many early `return`s but must always run a final
fix-up. A local struct with a destructor does it:

```cpp
struct FillUnplaced {
    std::array<std::vector<std::string>, 3>& layout;
    ~FillUnplaced() { /* append unplaced modules */ }
} fill_unplaced{layout_};
// every return below runs the destructor
```

### `if` with an initialiser

```cpp
if (auto it = bar.find("clock"); it != bar.end() && it->is_object()) { ... }
```

Declares `it`, tests it, and keeps it scoped to the `if`. Used for every
optional JSON sub-object.

### Deferring to the next loop iteration

Destroying a widget from inside its own signal handler crashes. Push the
work to the next iteration:

```cpp
Glib::signal_idle().connect_once([this] { rebuild(); });   // gtkmm
g_idle_add_once([](gpointer d) { rebuild(static_cast<Settings*>(d)); }, s); // C
```

### Pure functions where possible

`math_eval` and `fuzzy_score()` are plain functions with no state and no
I/O. They're trivial to test in a standalone `main()`. Prefer this shape
for any logic that doesn't need a widget or a socket.

---

## Lesson 17 — Working with C libraries from C++

### Learn

GTK, GLib, libpulse, gtk4-layer-shell and libadwaita are C libraries.
gtkmm wraps GTK and GLib in C++ classes; the rest you call directly. The
bridges:

- `gobj()` returns the underlying C object of any gtkmm wrapper:
  `gtk_layer_init_for_window(GTK_WINDOW(gobj()))`.
- C functions that take a callback also take a `void* user_data` you get
  back in the callback. Pass `this`, cast it back with `static_cast<Pulse*>(self)`.
- C strings you *receive* as `gchar*` must be freed with `g_free`. Strings
  you *pass* are `.c_str()`.
- `gpointer` is `void*`. `GINT_TO_POINTER(n)` / `GPOINTER_TO_INT(p)` stuff a
  small int into one.
- Formatting a double for CSS or Lua: `g_ascii_dtostr(buf, sizeof buf, x)`
  always uses `.`; `std::to_string` may use `,` in some locales.
- Logging: `g_message`, `g_warning`, `g_debug` (printf-style; pass strings
  as `.c_str()`).

### Try it

```cpp
// compile: g++ -std=c++20 clib.cpp $(pkg-config --cflags --libs glib-2.0) -o prog
#include <glib.h>
#include <iostream>
#include <string>

struct Counter { int hits = 0; };

// C-style callback: no `this`, so the object comes through user_data
static gboolean on_tick(gpointer user_data) {
    auto* counter = static_cast<Counter*>(user_data);
    ++counter->hits;
    g_message("tick %d", counter->hits);
    return counter->hits < 3;             // TRUE keeps the timer running
}

int main() {
    Counter counter;
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    g_timeout_add(200, on_tick, &counter);
    g_timeout_add(800, [](gpointer l) -> gboolean { g_main_loop_quit(static_cast<GMainLoop*>(l)); return FALSE; }, loop);
    g_main_loop_run(loop);                // the event loop from Lesson 15, for real

    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_dtostr(buf, sizeof buf, 0.88);
    std::cout << "css alpha: " << buf << "\n";
    g_main_loop_unref(loop);
}
```

### In hypr-shell

`Pulse` registers static callbacks with `this` as `userdata`. `App` formats
`bar.background_opacity` with `g_ascii_dtostr`. The settings app is *all*
C API: `g_signal_connect(row, "notify::active", G_CALLBACK(handler), s)`,
`g_object_set_data(G_OBJECT(w), "module-key", ptr)`.

**Exercise**: in the example, replace the capture-less lambda with a named
`static gboolean quit_loop(gpointer)` function, then put it back. Both must
compile; note that a lambda *with* captures would not.

---

## Lesson 18 — JSON with nlohmann

### Learn

`nlohmann::json` (header-only, `#include <nlohmann/json.hpp>`) represents a
JSON value that can be an object, array, string, number, bool or null.

```cpp
using json = nlohmann::json;
json j = json::parse(text, nullptr, /*allow_exceptions=*/false); // no throw; check j.is_object()
int n = j.value("windows", 0);                    // default if missing / wrong type
std::string s = j.value("name", std::string("?"));
if (auto it = j.find("clock"); it != j.end() && it->is_object()) { ... }
for (const auto& item : j) { ... }                // arrays
for (const auto& [key, val] : j.items()) { ... }  // objects
j["bar"]["position"] = "bottom";                  // create/modify
std::string out = j.dump(2);                      // pretty-print with indent 2
```

`j.value()` still throws if `j` itself isn't an object, so parsing untrusted
input is wrapped in `try/catch` anyway.

### Try it

```cpp
// compile: g++ -std=c++20 js.cpp -o prog   (header-only; on Arch: pacman -S nlohmann-json)
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

int main() {
    std::string text = R"({"bar": {"position": "left", "modules": {"clock": false}},
                           "junk": 12})";
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) { std::cout << "not an object\n"; return 1; }

    const json bar = j.value("bar", json::object());
    std::cout << "position: " << bar.value("position", "top") << "\n";
    std::cout << "opacity (missing → default): " << bar.value("background_opacity", 0.88) << "\n";

    if (auto it = bar.find("modules"); it != bar.end() && it->is_object())
        for (const auto& [name, enabled] : it->items())
            if (enabled.is_boolean()) std::cout << name << "=" << enabled.get<bool>() << "\n";

    j["bar"]["position"] = "bottom";              // like the settings app
    std::cout << j.dump(2) << "\n";               // "junk" is preserved
}
```

### In hypr-shell

`Config::load()` is a long sequence of `bar.value(...)` and
`bar.find(...)` calls, one per key. The settings app keeps the whole file in
`s->root`, edits one key per handler, and `dump(2)`s it back, which is why
unknown keys survive.

**Exercise**: parse `hyprctl -j workspaces` output (paste a real one into a
string) and print `id name windows` for each entry with `id >= 0`.

---

## Lesson 19 — Reading compiler and runtime errors

Errors are the compiler talking to you. Read the **first** error, fix it,
recompile; later errors are often consequences of the first.

| Message contains | Usual cause | Fix |
|------------------|-------------|-----|
| `undefined reference to hyprshell::X::X()` | `.cpp` not in `meson.build`, or method declared but never defined | add to `sources`, or write the body |
| `'Foo' was not declared in this scope` | missing `#include`, or missing `hyprshell::` / `std::` | include the header; qualify the name |
| `no matching function for call to` | wrong argument types or count; read the `candidate:` lines below | make the call match a candidate |
| `invalid use of incomplete type` | only a forward declaration is visible | include the full header |
| `marked 'override', but does not override` | typo in the method name or a different signature | copy the base method's signature exactly |
| `discards qualifiers` | calling a non-`const` method on a `const` object | mark the method `const` if it doesn't mutate |
| `use of deleted function` | copying a non-copyable object (a service, a widget) | use a reference: `auto&`, not `auto` |
| `is private within this context` | touching a private member from outside | add a getter |
| `comparison of integer expressions of different signedness` | `int` vs `unsigned`/`size_t` | cast one side deliberately |
| `unused variable` | leftover | delete it or use it |
| runtime `Gtk-CRITICAL ... assertion 'GTK_IS_WIDGET' failed` | destroyed/unparented widget, or wrong GObject cast | check lifetime; check the cast macro |
| runtime `std::bad_cast` | `cast_dynamic<Glib::Variant<T>>` with the wrong `T` | match the DBus signature |
| runtime `terminate called after throwing` | exception escaped a callback | wrap the callback body in `try/catch` |
| runtime `Finalizing ... still has children` | popover or managed child not unparented in a destructor | `unparent()` it |

Warnings are on at level 2 in the project. Fix them as you go; a clean
build is the norm here.

---

## Lesson 20 — Guided reading of real files

Read these in order, with the lessons above as your decoder. For each,
write one sentence per function saying what it does before you move on.

1. `src/bar/modules/clock.cpp` (~100 lines): a `Gtk::Label` subclass,
   config reading, a timer, a popover. Lessons 8, 11, 12, 15.
2. `src/bar/modules/volume.cpp`: a `Gtk::Box` with an icon label, two
   gesture controllers, service subscription, glyph choice. Lesson 3.
3. `src/services/power_profiles.cpp` (~60 lines): the smallest DBus
   service: proxy creation, property read, optimistic write. Lessons 13,
   14, 16.
4. `src/services/config.cpp`: JSON parsing, defaults, the RAII fix-up
   struct, the layout resolver. Lessons 6, 16, 18.
5. `src/services/hyprland.cpp`: the async socket chain and the event
   stream. Lesson 15.
6. `src/bar/bar.cpp`: layer-shell calls, rebuilding boxes from config, the
   auto-hide animation. Lesson 17 plus the [GTK tutorial](16-gtk-tutorial.md).
7. `src/settings/main.cpp`, first 200 lines and one handler: the C API
   side. Lesson 17.

If something in these files isn't covered by a lesson, that's a gap in
this course: note it and look it up on cppreference, then consider adding
a paragraph here.

---

## Lesson 21 — Capstone

You're ready to change the project.

1. **Warm-up**: make the volume tooltip say "Muted" when muted (`Volume::update()`).
2. **Getter**: expose the raw sink name from `Pulse` and show it in the audio
   panel tooltip.
3. **The module**: follow [Tutorial: add a bar module](05-tutorial-new-module.md)
   and build the system-load module end to end, including its setting and
   settings-app row.
4. **Variation**: change it to read `/proc/meminfo` and show used-memory
   percent. Parse with `find` and `substr` (Lesson 2) or `std::stoi`.
5. **Break it on purpose**: remove the serial check in
   `Workspaces::refresh()`, switch workspaces rapidly, watch stale state,
   restore it. Then comment out `popover_.unparent()` in `Clock::~Clock`,
   set `bar.visibility` to `hidden` and back, read the GTK critical, restore.
6. **Refactor**: extract the four repeated "open popover on the free side of
   the bar" `switch` blocks in the modules into one helper function that
   takes a `Gtk::Popover&`. Behaviour must not change.

When exercises 3 and 6 are done and the tree compiles clean, you know all
the C++ this project needs. The one thing not covered is threads, which the
project avoids on purpose; the lock screen's PAM call will need exactly one,
and the [roadmap guide](13-roadmap-guide.md) describes how to isolate it.
