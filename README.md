# easyweb

**Small, fast and friendly web programming.**

easyweb is a tiny web language and toolkit. You write one `.eweb` file, and easyweb turns it
into a real website: a server, an API, and the JavaScript that runs in the browser.

```easyweb
page "my notes"
port 8080

route GET "/api/notes" {
  return { ok: true, notes: [] }
}

ui {
  <h1>hello world</h1>
}

script {
  EW.text("h1", "it works!")
}
```

```bash
easyweb run op notes.eweb
```

That is the whole idea: one file, one command, a working website.

---

## Contents

- [Install](#install)
- [Your first app](#your-first-app)
- [The easyweb language](#the-easyweb-language)
  - [The page](#the-page)
  - [The user interface](#the-user-interface)
  - [The script](#the-script)
  - [Routes and the API](#routes-and-the-api)
  - [Data](#data)
  - [Styles](#styles)
  - [Functions](#functions)
  - [Kid mode](#kid-mode)
- [Built-in functions](#built-in-functions)
- [Packages](#packages)
- [Command reference](#command-reference)
- [Project layout](#project-layout)
- [Building from source](#building-from-source)
- [License](#license)

---

## Install

### Windows

Download `easyweb.exe` and `webhelp.exe` from the
[releases page](https://github.com/click-for-games/easyweb/releases) and put them next to each
other in a folder. Add that folder to your `PATH` and you are done.

```bash
easyweb version
webhelp version
```

### macOS and Linux

```bash
./build.sh          # builds dist/easyweb and dist/webhelp
```

or by hand:

```bash
CORE="src/util.c src/lexer.c src/ast.c src/parser.c src/value.c src/interp.c src/jsgen.c src/kid.c src/render.c src/http.c src/server.c src/pkg.c src/packages.c src/highlight.c src/json.c src/webhelp.c"
gcc -O2 -std=c11 -o easyweb $CORE src/main.c -lm
gcc -O2 -std=c11 -o webhelp $CORE src/webhelp_main.c -lm
```

---

## Your first app

```bash
webhelp init notes      # makes notes/notes.eweb
cd notes
easyweb run op notes.eweb
```

`op` means "open": easyweb starts the server and opens your browser when it is ready.
Drop it if you do not want the browser to pop up.

The starter app already shows a page, an API and a button that talks to that API.

---

## The easyweb language

An easyweb file is made of small blocks. Each block starts with a keyword.

| Block | What it does |
| --- | --- |
| `page "title"` | the title of the page |
| `port 8080` | the port the server listens on |
| `use <package>` | load a package |
| `route GET "/path" { }` | a server endpoint |
| `ui { }` | the HTML of the page |
| `script { }` | the JavaScript of the page |
| `style { }` | the CSS of the page |
| `data { }` | data for the page |
| `kid { }` | kid mode blocks |

### The page

```easyweb
page "todo list"
port 3000
```

`port` is optional, it defaults to `8080`.

### The user interface

The `ui` block is plain HTML. easyweb puts it inside a clean, ready to use page shell.

```easyweb
ui {
  <div class="card">
    <h1>My notes</h1>
    <input id="note" placeholder="write a note...">
    <button id="add">Add it</button>
    <ul id="list"></ul>
  </div>
}
```

### The script

The `script` block is JavaScript-ish code that easyweb compiles for the browser.
It knows about the DOM through the `EW` helper object.

```easyweb
script {
  EW.on("#add", "click", function () {
    var text = EW.val("#note")
    EW.post("/api/notes", { text: text }, function (res) {
      EW.text("#status", res.ok ? "saved!" : res.error)
    })
  })
}
```

Everything inside `script { }` runs in the browser. Server code goes into `route { }` blocks.

### Routes and the API

A route is a folder of the website that answers with data instead of HTML.

```easyweb
route GET "/api/notes" {
  return { ok: true, notes: db.get("notes", []) }
}

route POST "/api/notes" {
  let body = req.body
  db.push("notes", body.text)
  return { ok: true, notes: db.get("notes", []) }
}

route DELETE "/api/notes" {
  db.set("notes", [])
  return { ok: true }
}
```

Inside a route you get `req`:

| `req` | What it is |
| --- | --- |
| `req.method` | `GET`, `POST`, ... |
| `req.path` | the path that was asked for |
| `req.query` | the query string as an object, for example `req.query.page` |
| `req.params` | the `:id` parts of the path |
| `req.headers` | the request headers |
| `req.body` | the request body, parsed as JSON when possible |
| `req.raw_body` | the request body as plain text |

Returning an object sends JSON. You can also use `status(404)`, `header("X-thing", "1")`,
`redirect("/somewhere")` and `send("raw text")`.

### Data

```easyweb
data {
  visits: 0,
  name: "easyweb"
}
```

The `data` block becomes the `data` object in the browser.

### Styles

```easyweb
style {
  .card { border-radius: 12px; padding: 24px; }
}
```

### Functions

```easyweb
func greet(name) {
  return "hello " + name
}

route GET "/api/greet" {
  return { message: greet(req.query.name) }
}
```

A function that should only live in the browser is marked with `client`:

```easyweb
client func shout(text) {
  return text.toUpperCase()
}
```

### Kid mode

Kid mode is a friendly way to write small pages, with big buttons and soft errors.
Start easyweb with `--kid` (or `easyweb run --kid app.eweb`).

```easyweb
kid {
  BUTTON _ <say hello!>
  BUTTON _ <tell me a joke>
}
```

`_` means "easyweb, pick a name for me". Kid mode also has its own playground window
that shows what your blocks do while you build.

---

## Built-in functions

### On the server

| Function | What it does |
| --- | --- |
| `print(x)` / `log(x)` | print to the server console |
| `len(x)` | length of a string, list or object |
| `type(x)` | `"string"`, `"number"`, `"array"`, `"object"`, ... |
| `str(x)` / `num(x)` / `int(x)` / `bool(x)` | change the type of a value |
| `json(x)` | turn a value into JSON text |
| `parse_json(text)` | turn JSON text into a value |
| `now()` | the current time |
| `range(n)` | a list of numbers, `range(1, 5)` |
| `keys(obj)` / `values(obj)` | the keys or values of an object |
| `push(list, x)` / `pop(list)` | add to or remove from a list |
| `upper(s)` / `lower(s)` / `trim(s)` | tidy up a string |
| `split(s, sep)` / `join(list, sep)` | cut and glue strings |
| `abs(n)` / `round(n)` / `floor(n)` / `ceil(n)` | number helpers |
| `min(...)` / `max(...)` | smallest or biggest number |
| `escape(s)` | make a string safe for HTML |
| `status(code)` | set the HTTP status of the answer |
| `header(name, value)` | set an HTTP header |
| `redirect(url)` | send the visitor somewhere else |
| `html(text)` | answer with HTML |
| `send(text)` | answer with plain text |
| `read_file(path)` | read a file next to your app |
| `write_file(path, text)` | write a file next to your app |
| `file_exists(path)` | is there a file with that name? |
| `app_name()` / `app_dir()` | the name and folder of your app |
| `random(n)` | a random number |

### In the browser

Everything in the `script { }` block can use the `EW` object:

`EW.on(sel, event, fn)`, `EW.get(url, cb)`, `EW.post(url, body, cb)`, `EW.put(...)`,
`EW.del(...)`, `EW.fetch(url, opts)`, `EW.text(sel, value)`, `EW.html(sel, value)`,
`EW.val(sel)` / `EW.val(sel, value)`, `EW.addClass(sel, cls)`, `EW.removeClass(sel, cls)`,
`EW.toggleClass(sel, cls)`, `EW.show(sel)`, `EW.hide(sel)`, `EW.toast(message)`,
`EW.json(value)`, `EW.parse(text)`, `EW.len(value)`, `EW.str(value)`, `EW.num(value)`,
`EW.type(value)`, `EW.log(...)`, `EW.alert(message)`.

---

## Packages

Packages add extra words to your app. They are installed with `webhelp`.

```bash
webhelp install animateeasy   # smooth CSS animations
webhelp install dataweb       # a tiny file based database
```

Then use them in your app:

```easyweb
use dataweb
```

### dataweb

`dataweb` saves your data in a `.ewbdata` file right next to your app, so
`notes.eweb` gets `notes.ewbdata`. No server, no setup.

```easyweb
db.set("notes", ["hello"])   # save a list
db.push("notes", "world")    # add one more
db.get("notes", [])          # read it back
db.all()                     # everything
db.remove("notes")           # drop one key
db.keys()                    # the key names
db.count()                   # how many keys
db.save()                    # write to disk now
db.file()                    # where the file is
```

### animateeasy

```easyweb
use animateeasy

script {
  animate("#box", "fade-in")
  animate("#box", "bounce", 1200)
}
```

---

## Command reference

### `easyweb`

| Command | What it does |
| --- | --- |
| `easyweb run [op] <file.eweb>` | run your app on `http://localhost:8080` |
| `easyweb build <file.eweb>` | check the app and export a static page (`-o <file.html>`) |
| `easyweb check <file.eweb>` | check the code without running it |
| `easyweb highlight <file.eweb>` | show the file with easyweb colours (`--html` for HTML) |
| `easyweb new <name>` | make a new easyweb project |
| `easyweb packages` | list the packages you can install |
| `easyweb webhelp <cmd>` | the package manager |
| `easyweb version` | show the version |
| `easyweb help` | show the help |

Options for `run`:

| Option | What it does |
| --- | --- |
| `--port <n>` | use a different port |
| `--host <ip>` | bind to another ip |
| `--kid` | friendly kid mode with soft errors |
| `--op` | open the browser when the server is ready |
| `--no-open` | never open the browser |

Examples:

```bash
easyweb run op hello.eweb
easyweb run --port 3000 myapp.eweb
easyweb run --host 0.0.0.0 myapp.eweb
easyweb build myapp.eweb
```

### `webhelp`

| Command | What it does |
| --- | --- |
| `webhelp install <package>` | install a package into this project |
| `webhelp install <package> --global` | install it for every project |
| `webhelp remove <package>` | remove a package |
| `webhelp list` | show the installed packages |
| `webhelp search [word]` | look for a package |
| `webhelp info <package>` | show details about a package |
| `webhelp init [name]` | make a new easyweb project |
| `webhelp version` | show the version |
| `webhelp help` | show the help |

Examples:

```bash
webhelp install animateeasy
webhelp install dataweb --global
webhelp list
webhelp search data
webhelp info dataweb
webhelp remove dataweb
```

---

## Project layout

A project is a folder with your app and, if you use packages, a small `.easyweb` folder:

```
notes/
  notes.eweb            your app
  notes.ewbdata         your data (created by dataweb)
  .easyweb/
    packages/
      dataweb/
        dataweb.eweb
        dataweb.css
        dataweb.js
        manifest.json
```

---

## Building from source

easyweb is plain C11 with no dependencies beyond the standard library.

```bash
# macOS and Linux
./build.sh

# Windows
powershell -ExecutionPolicy Bypass -File build.ps1

# or by hand
CORE="src/util.c src/lexer.c src/ast.c src/parser.c src/value.c src/interp.c src/jsgen.c src/kid.c src/render.c src/http.c src/server.c src/pkg.c src/packages.c src/highlight.c src/json.c src/webhelp.c"
gcc -O2 -std=c11 -o easyweb $CORE src/main.c -lm
```

The build produces `easyweb.exe` and `webhelp.exe` in `dist/`.

The sources are in `src/`:

| File | What it is |
| --- | --- |
| `src/main.c` | the `easyweb` command line |
| `src/webhelp.c`, `src/webhelp_main.c` | the `webhelp` package manager |
| `src/lexer.c`, `src/parser.c`, `src/ast.c` | turning text into a tree |
| `src/value.c` | values, lists, objects and environments |
| `src/interp.c` | the server side interpreter |
| `src/jsgen.c` | the browser side JavaScript generator |
| `src/render.c` | putting the page together |
| `src/server.c`, `src/http.c` | the web server |
| `src/kid.c` | kid mode |
| `src/pkg.c`, `src/packages.c` | packages |
| `src/highlight.c` | colours for the terminal |
| `src/json.c` | JSON |

---

## License

MIT. See [LICENSE](LICENSE).
