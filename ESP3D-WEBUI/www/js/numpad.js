var numpad = {
  // (A) CREATE NUMPAD HTML
  hwrap: null, // numpad wrapper container
  hpad: null, // numpad itself
  hdisplay: null, // number display
  hbwrap: null, // buttons wrapper
  hbuttons: {}, // individual buttons
  // Fraction-mode state
  isFractionMode: false, // true when axis supports fractional inch entry
  nowAxis: null,         // current axis ('Z', 'D', etc.)

  init: function(){
    // (A1) WRAPPER
    numpad.hwrap = document.createElement("div");
    numpad.hwrap.id = "numWrap";


    // (A2) ENTIRE NUMPAD ITSELF
    numpad.hpad = document.createElement("div");
    numpad.hpad.id = "numPad";
    numpad.hwrap.appendChild(numpad.hpad);
    numpad.hpad.tabindex = "0";
    numpad.hpad.contentEditable = false;
    numpad.hpad.addEventListener("keydown", numpad.keypr);

    // (A3) DISPLAY
    numpad.hdisplay = document.createElement("input");
    numpad.hdisplay.id = "numDisplay";
    numpad.hdisplay.type = "text";
    numpad.hdisplay.disabled = true;
    numpad.hdisplay.value = "0";
    numpad.hpad.appendChild(numpad.hdisplay);



    // (A4) NUMBER BUTTONS
    numpad.hbwrap = document.createElement("div");
    numpad.hbwrap.id = "numBWrap";
    numpad.hpad.appendChild(numpad.hbwrap);

    // (A5) BUTTONS
    var buttonator = function (txt, css, fn) {
      var button = document.createElement("div");
      button.innerHTML = txt;
      button.classList.add(css);
      button.addEventListener("click", fn);
      numpad.hbwrap.appendChild(button);
      numpad.hbuttons[txt] = button;
      return button;
    };

    var spacer = function() {
      buttonator("", "spacer", null);
    };

    // 7 8 9 _ Goto
    for (var i=7; i<=9; i++) { buttonator(i, "num", numpad.digit); }
    buttonator("&#10502;", "del", numpad.delete);
    spacer();
    buttonator("", "num", numpad.doNothing);

    // 4 5 6 C _ _
    for (var i=4; i<=6; i++) { buttonator(i, "num", numpad.digit); }
    buttonator("C", "clr", numpad.reset);
    spacer();
    spacer();

    // 1 2 3 +- Set
    for (var i=1; i<=3; i++) { buttonator(i, "num", numpad.digit); }
    buttonator("", "num", numpad.doNothing);
    buttonator("Set", "set", numpad.setCoordinate);

    // 0 . [space] [/] Cancel
    buttonator(0, "zero", numpad.digit);
    numpad.hbuttons["dot"] = buttonator(".", "dot", numpad.dot);
    numpad.hbuttons["space"] = buttonator("&#9251;", "frac-space", numpad.space);
    numpad.hbuttons["slash"] = buttonator("/", "frac-slash", numpad.slash);
    buttonator("Cancel", "cxwide", numpad.hide);


    // (A6) ATTACH NUMPAD TO HTML BODY
    document.body.appendChild(numpad.hwrap);
  },

    // (B) BUTTON ACTIONS
    // (B1) CURRENTLY SELECTED FIELD + MAX LIMIT
    nowTarget: null, // Current selected input field
    nowMax: 0, // Current max allowed digits

    keypr: function(event) {
        event.preventDefault();
        switch(event.key) {
            case "Escape":
            case "q":
                numpad.hide();
                break;
            case "0":
            case "1":
            case "2":
            case "3":
            case "4":
            case "5":
            case "6":
            case "7":
            case "8":
            case "9":
                numpad.digitv(event.key);
                break;
            case '.':
                numpad.dot();
                break;
            case ' ':
                if (numpad.isFractionMode) numpad.space();
                break;
            case '/':
                if (numpad.isFractionMode) numpad.slash();
                break;
            case 'Backspace':
            case 'Del':
                numpad.delete();
                break;
            case 'x':
            case 'X':
                numpad.reset();
                break;
            case 'c':
            case 'C':
                numpad.reset();
                break;
            case 'g':
            case 'G':
                numpad.recall();
                break;
            case 's':
            case 'S':
            case 'Enter':
                numpad.setCoordinate();
                break;
        }
    },

    // (B2) NUMBER (0 TO 9)

    digitv: function(n) {
        var current = numpad.hdisplay.value;
        if (current.length < numpad.nowMax) {
            if (current === "0") {
                numpad.hdisplay.value = n;
            } else {
                numpad.hdisplay.value += n;
            }
        }
    },

    digit: function() {
        numpad.digitv(this.innerHTML);
    },

    // Change sign
    toggleSign: function(){
        numpad.hdisplay.value = -numpad.hdisplay.value;
    },

    // Do nothing function
    doNothing: function(){},


    // ADD DECIMAL POINT
    dot: function(){
        if (numpad.isFractionMode) return; // no decimal point in fraction mode
        if (numpad.hdisplay.value.indexOf(".") === -1) {
            if (numpad.hdisplay.value === "0") {
                numpad.hdisplay.value = "0.";
            } else {
                numpad.hdisplay.value += ".";
            }
        }
    },

    // ADD SPACE (fraction mode: separates whole from numerator)
    space: function(){
        if (!numpad.isFractionMode) return;
        var current = numpad.hdisplay.value;
        // Only allow one space, and it must come before the slash
        if (current.indexOf(" ") === -1 && current.indexOf("/") === -1 && current !== "0") {
            numpad.hdisplay.value += " ";
        }
    },

    // ADD SLASH (fraction mode: separates numerator from denominator)
    slash: function(){
        if (!numpad.isFractionMode) return;
        var current = numpad.hdisplay.value;
        // Only allow one slash, and the display must not be "0" or empty
        if (current.indexOf("/") === -1 && current !== "0" && current !== "") {
            numpad.hdisplay.value += "/";
        }
    },

    // BACKSPACE
    delete: function(){
    var length = numpad.hdisplay.value.length;
    if (length == 1) { numpad.hdisplay.value = 0; }
    else { numpad.hdisplay.value = numpad.hdisplay.value.substring(0, length - 1); }
  },

  // (B5) CLEAR ALL
  reset: function(){ numpad.hdisplay.value = "0"; },

  // (B6) Recall
  recall: function(){
    numpad.hdisplay.value = numpad.nowTarget.textContent;
  },

  setCoordinate: function(){
    var val = numpad.hdisplay.value;

    if (numpad.isFractionMode) {
      // Validate the fraction before accepting
      var errMsg = typeof validateInchFracInput === 'function'
        ? validateInchFracInput(val, numpad.nowAxis)
        : null;
      if (errMsg) {
        // Show error in display briefly
        var prev = numpad.hdisplay.value;
        numpad.hdisplay.value = errMsg.slice(0, 28);
        numpad.hdisplay.style.color = "red";
        setTimeout(function() {
          numpad.hdisplay.value = prev;
          numpad.hdisplay.style.color = "";
        }, 2000);
        return;
      }
    }

    numpad.nowTarget.textContent = val;
    saveJogDists();
    numpad.hide();
  },

  gotoCoordinate: function(){
    numpad.nowTarget.textContent = numpad.hdisplay.value;
    goAxisByValue(numpad.nowTarget.dataset.axis, numpad.hdisplay.value);
    numpad.hide();
  },

  // (C) ATTACH NUMPAD TO INPUT FIELD
  attach: function(opt){
  // OPTIONS
  //  target: required, ID of target field.
  //  max: optional, maximum number of characters. Default 255.
  //  decimal: optional, allow decimal? Default true.

    // (C1) DEFAULT OPTIONS
    if (opt.max === undefined) { opt.max = 255; }
    if (opt.decimal === undefined) { opt.decimal = true; }

    // (C2) GET + SET TARGET OPTIONS
    var target = document.getElementById(opt.target);
    target.readOnly = true;
    target.dataset.max = opt.max;
    target.dataset.decimal = opt.decimal;
    target.dataset.axis = opt.axis;
    target.dataset.elementName = opt.target;
    target.addEventListener("click", numpad.show);
  },

  // (D) SHOW NUMPAD
  show: function() {

    // (D1) SET CURRENT DISPLAY VALUE
    var cv = "";
    if (cv == "") { cv = "0"; }
    numpad.hdisplay.value = cv;

    // (D2) SET MAX ALLOWED CHARACTERS
    numpad.nowMax = this.dataset.max;

    // (D3) SET DECIMAL / FRACTION MODE
    var axis = this.dataset.axis || "";
    numpad.nowAxis = axis;
    var inFracMode = typeof unitDisplayMode !== "undefined" && unitDisplayMode === "inch_frac";
    numpad.isFractionMode = inFracMode;

    if (inFracMode) {
      // Show space and slash buttons; hide decimal point button
      numpad.hbwrap.classList.add("fracMode");
      numpad.hbwrap.classList.remove("noDec");
      // Show a hint in the display area title
      numpad.hdisplay.title = 'Enter: whole fraction e.g. "1 3/16"';
    } else {
      numpad.hbwrap.classList.remove("fracMode");
      if (this.dataset.decimal == "true") {
        numpad.hbwrap.classList.remove("noDec");
      } else {
        numpad.hbwrap.classList.add("noDec");
      }
      numpad.hdisplay.title = "";
    }

    // (D4) SET CURRENT TARGET
    numpad.nowTarget = this;

    // (D5) SHOW NUMPAD
    numpad.hwrap.classList.add("open");

    // numpad.hpad.focus();
  },

  // (E) HIDE NUMPAD
  hide: function(){ numpad.hwrap.classList.remove("open"); },
};
window.addEventListener("DOMContentLoaded", numpad.init);
