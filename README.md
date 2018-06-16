# sqt
`sqt` (sql query tool) is a cross-platform program to provide typical sql data source exploring and programming interface.

## Overview
The subject was aimed to provide fast and convenient MS SQL query tool under linux. The only existing ODBC interface lead to support of any other ODBC data source. As a result of pgAdmin3 deprecation `sqt` was modified to have a native PostgreSQL support via libpq. Due to my current needs PostgreSQL support is in priority.
The main target audience are db programmers.
#### Binaries
Standalone [sqt for windows x64](https://drive.google.com/open?id=1pD-twf3N0svQv-UzolVLlaUl3PZ1GK8U) (~12Mb)

## Feature highlights
* Customizable objects tree and textual/tabular content view (see [scripts/README.md](https://github.com/parihaaraka/sqt/blob/master/scripts/README.md)) let you build you own tree with application-specific nodes;
* adjustable sql highlighting (see any existing `hl.conf` for more details);
* alternative sorting (e.g., sort table columns in original/alphabetical order);
* multiple selection to generate appropriate SQL code (e.g., select/insert/update commands with selected columns only); 
* convenient (Qt Creator-like) search/replace panel;
* multiple resultsets support;
* bookmarks (mark: `Ctrl+M`, previous: `Ctrl+,`, next: `Ctrl+.`, last: `Ctrl+L`);
* initial code completion support (`Ctrl+Space`);
* json viewer with highlighting and extracting json from it's nested text value (`Ctrl+J`);
* totalling selected cells (`F6`);
* resultsets structure textual output;
* pg: client-side COPY to file or log widget, COPY from file;
* pg: receiving notifications (`NOTIFY`) and messages (`RAISE`).

![screenshot](https://github.com/parihaaraka/sqt/wiki/img/screenshot1.png)

## Build instruction
You may build the project by means of QtCreator or execute this sequence of commands from the project's root directory:
```
mkdir build && cd build && qmake ../src/sqt.pro && make
```
Qt toolchain must be installed and be available via PATH.

## Todo
* Improve code completion, prepare scripts to fetch metadata from not-postgresql data sources;
* executing of JavaScript along with SQL to run automation tasks;
* new script types to extend objects tree interaction (modifying, administration tasks);
* enhance scripts to make `sqt` as useful as possible (+provide scripts for different dbms, versions, generic odbc data source);
* batch mode;
* reports.

### Acknowledgment
Some icons by [Yusuke  Kamiyamane](http://p.yusukekamiyamane.com). All rights reserved.

