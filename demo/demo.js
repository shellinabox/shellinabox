// Demo.js -- Demonstrate some of the features of ShellInABox
// Copyright (C) 2008-2009 Markus Gutschke <markus@shellinabox.com>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// In addition to these license terms, the author grants the following
// additional rights:
//
// If you modify this program, or any covered work, by linking or
// combining it with the OpenSSL project's OpenSSL library (or a
// modified version of that library), containing parts covered by the
// terms of the OpenSSL or SSLeay licenses, the author
// grants you additional permission to convey the resulting work.
// Corresponding Source for a non-source form of such a combination
// shall include the source code for the parts of OpenSSL used as well
// as that of the covered work.
//
// You may at your option choose to remove this additional permission from
// the work, or from any part of it.
//
// It is possible to build this program in a way that it loads OpenSSL
// libraries at run-time. If doing so, the following notices are required
// by the OpenSSL and SSLeay licenses:
//
// This product includes software developed by the OpenSSL Project
// for use in the OpenSSL Toolkit. (http://www.openssl.org/)
//
// This product includes cryptographic software written by Eric Young
// (eay@cryptsoft.com)
//
//
// The most up-to-date version of this program is always available from
// http://shellinabox.com
//
//
// Notes:
//
// The author believes that for the purposes of this license, you meet the
// requirements for publishing the source code, if your web server publishes
// the source in unmodified form (i.e. with licensing information, comments,
// formatting, and identifier names intact). If there are technical reasons
// that require you to make changes to the source code when serving the
// JavaScript (e.g to remove pre-processor directives from the source), these
// changes should be done in a reversible fashion.
//
// The author does not consider websites that reference this script in
// unmodified form, and web servers that serve this script in unmodified form
// to be derived works. As such, they are believed to be outside of the
// scope of this license and not subject to the rights or restrictions of the
// GNU General Public License.
//
// If in doubt, consult a legal professional familiar with the laws that
// apply in your country.

// #define STATE_IDLE     0
// #define STATE_INIT     1
// #define STATE_PROMPT   2
// #define STATE_READLINE 3
// #define STATE_COMMAND  4
// #define STATE_EVAL     5
// #define STATE_RUN      6
// #define STATE_EXEC     7

function extend(subClass, baseClass) {
  function inheritance() { }
  inheritance.prototype          = baseClass.prototype;
  subClass.prototype             = new inheritance();
  subClass.prototype.constructor = subClass;
  subClass.prototype.superClass  = baseClass.prototype;
};

function Demo(container) {
  this.superClass.constructor.call(this, container);
  this.gotoState(1 /* STATE_INIT */);
};
extend(Demo, VT100);

Demo.prototype.keysPressed = function(ch) {
  this.keys += ch;
  this.gotoState(this.state);
};

Demo.prototype.gotoState = function(state, tmo) {
  this.state   = state;
  if (!this.timer || tmo) {
    if (!tmo) {
      tmo      = 1;
    }
    this.timer = setTimeout(function(demo) {
                              return function() {
                                demo.demo();
                              };
                            }(this), tmo);
  }
};

Demo.prototype.demo = function() {
  var done                  = false;
  while (!done) {
    var state               = this.state;
    this.state              = 0 /* STATE_IDLE */;
    switch (state) {
    case 1 /* STATE_INIT */:
      this.doInit();
      break;
    case 2 /* STATE_PROMPT */:
      this.doPrompt();
      break;
    case 3 /* STATE_READLINE */:
      this.doReadLine();
      if (this.state == 3 /* STATE_READLINE */) {
        done                = true;
      }
      break;
    case 4 /* STATE_COMMAND */:
      this.doCommand();
      break;
    case 5 /* STATE_EVAL */:
      this.doEval();
      break;
    case 6 /* STATE_RUN */:
      this.doRun();
      break;
    case 7 /* STATE_EXEC */:
      if (this.doExec()) {
        return;
      }
      break;
    case 0 /* STATE_IDLE */:
    default:
      done                  = true;
      break;
    }
  }
  this.timer   = undefined;
};

Demo.prototype.error = function(msg) {
  if (msg == undefined) {
    msg = 'Syntax Error';
  }
  this.vt100('\u0007? ' + msg + '\r\n');
  this.gotoState(2 /* STATE_PROMPT */);
};

Demo.prototype.doInit = function() {
  this.program = new Array();
  this.vt100(
    '\u001Bc\u001B[34;4m' +
    'ShellInABox Demo Script\u001B[24;31m\r\n' +
    '\r\n' +
    'Copyright 2009 by Markus Gutschke <markus@shellinabox.com>\u001B[0m\r\n' +
    '\r\n' +
    '\r\n' +
    'This script simulates a minimal BASIC interpreter, allowing you to\r\n' +
    'experiment with the JavaScript terminal emulator that is part of\r\n' +
    'the ShellInABox project.\r\n' +
    '\r\n' +
    'Type HELP for a list of commands.\r\n' +
    '\r\n');
  this.gotoState(2 /* STATE_PROMPT */);
};

Demo.prototype.doPrompt = function() {
  this.keys             = '';
  this.line             = '';
  this.currentLineIndex = -1;
  this.vt100('> ');
  this.gotoState(3 /* STATE_READLINE */);
};

Demo.prototype.doReadLine = function() {
  this.gotoState(3 /* STATE_READLINE */);
  var keys  = this.keys;
  this.keys = '';
  for (var i = 0; i < keys.length; i++) {
    var ch  = keys.charAt(i);
    if (ch >= ' ' && ch < '\u007F' || ch > '\u00A0') {
      this.line += ch;
      this.vt100(ch);
    } else if (ch == '\r' || ch == '\n') {
      this.vt100('\r\n');
      this.gotoState(4 /* STATE_COMMAND */);
    } else if (ch == '\u0008' || ch == '\u007F') {
      if (this.line.length > 0) {
        this.line = this.line.substr(0, this.line.length - 1);
        if (this.cursorX == 0) {
          var x = this.terminalWidth - 1;
          var y = this.cursorY - 1;
          this.gotoXY(x, y);
          this.vt100(' ');
          this.gotoXY(x, y);
        } else {
          this.vt100('\u0008 \u0008');
        }
      } else {
        this.vt100('\u0007');
      }
    } else if (ch == '\u001B') {
      // This was probably a function key. Just eat all of the following keys.
      break;
    }
  }
};

Demo.prototype.doCommand = function() {
  this.gotoState(2 /* STATE_PROMPT */);
  var tokens              = new Tokens(this.line);
  this.line               = '';
  var cmd                 = tokens.nextToken().toUpperCase();
  if (cmd.match(/^[0-9]+$/)) {
    tokens.removeLineNumber();
    var lineNumber        = parseInt(cmd);
    var index             = this.findLine(lineNumber);
    if (tokens.nextToken() == undefined) {
      if (index > 0) {
        // Delete line from program
        this.program.splice(index, 1);
      }
    } else {
      if (index >= 0) {
        // Replace line in program
        this.program[index].setTokens(tokens);
      } else {
        // Add new line to program
        this.program.splice(-index - 1, 0, new Line(lineNumber, tokens));
      }
    }
  } else {
    this.currentLineIndex = -1;
    this.tokens           = tokens;
    this.gotoState(5 /* STATE_EVAL */);
  }
  tokens.reset();
};

Demo.prototype.doEval = function() {
  this.gotoState(2 /* STATE_PROMPT */);
  var cmd = this.tokens.nextToken().toUpperCase();
  if (cmd == "HELP") {
    this.vt100('Supported commands:\r\n' +
               '  HELP LIST RUN\r\n');
  } else if (cmd == "LIST") {
    if (this.tokens.nextToken() != undefined) {
      this.error();
      return false;
    } else {
      for (var i = 0; i < this.program.length; i++) {
        var line = this.program[i];
        this.vt100('' + line.lineNumber());
        line.tokens().reset();
        for (var token; (token = line.tokens().nextToken()) != undefined; ) {
          this.vt100(' ' + token);
        }
        line.tokens().reset();
        this.vt100('\r\n');
      }
    }
  } else if (cmd == "RUN") {
    if (this.tokens.nextToken() != undefined) {
      this.error();
    } else {
      this.gotoState(6 /* STATE_RUN */);
    }
    return false;
  } else {
    this.error();
    return false;
  }
  return true;
};

Demo.prototype.doRun = function() {
  if (this.program.length > 0) {
    this.currentLineIndex = 0;
    this.gotoState(7 /* STATE_EXEC */);
  }
};

Demo.prototype.doExec = function() {
  this.tokens = this.program[this.currentLineIndex++].tokens();
  this.tokens.reset();
  if (this.doEval()) {
    if (this.currentLineIndex >= this.program.length) {
      this.gotoState(2 /* STATE_PROMPT */);
      return false;
    } else {
      this.gotoState(7 /* STATE_EXEC */, 20);
      return true;
    }
  }
};

Demo.prototype.findLine = function(lineNumber) {
  var l   = 0;
  var h   = this.program.length;
  while (h > l) {
    var m = Math.floor((l + h) / 2);
    var n = this.program[m].lineNumber();
    if (n == lineNumber) {
      return m;
    } else if (n > lineNumber) {
      h   = m;
    } else {
      l   = m + 1;
    }
  }
  return -l - 1;
};

function Tokens(line) {
  this.tokens = line.split(' ');
  this.index  = 0;
};

Tokens.prototype.nextToken = function() {
  while (this.index < this.tokens.length) {
    var token = this.tokens[this.index++];
    token     = token.replace(/^[ \t]*/, '');
    token     = token.replace(/[ \t]*$/, '');
    if (token.length) {
      return token;
    }
  }
  return undefined;
};

Tokens.prototype.removeLineNumber = function() {
  if (this.tokens.length > 0) {
    this.tokens.splice(0, 1);
    if (this.index > 0) {
      this.index--;
    }
  }
};

Tokens.prototype.reset = function() {
  this.index = 0;
};

function Line(lineNumber, tokens) {
  this.lineNumber_ = lineNumber;
  this.tokens_     = tokens;
};

Line.prototype.lineNumber = function() {
  return this.lineNumber_;
};

Line.prototype.tokens = function() {
  return this.tokens_;
};

Line.prototype.setTokens = function(tokens) {
  this.tokens_ = tokens;
};

Line.prototype.sort = function(a, b) {
  return a.lineNumber_ - b.lineNumber_;
};
