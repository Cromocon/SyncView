// Script KWin temporaneo (vedi tools/on-desktop9.sh): sposta sul «Desktop 9» le finestre dei test e di SyncView appena compaiono.
// Su Wayland la classe della finestra può arrivare DOPO windowAdded (e le finestre transient/modali hanno la propria):
// si ricontrolla a ogni cambio di classe/nome del file desktop e si segue il genitore (transientFor).
var target = workspace.desktops.filter(function (d) { return d.name === "Desktop 9"; })[0];
var pattern = /(^|\s)test_|syncview/i;
function idOf(w) {
    return ((w.resourceClass || "") + " " + (w.resourceName || "") + " " + (w.desktopFileName || "")).trim();
}
function place(w) {
    if (!w || !target) { return; }
    var id = idOf(w);
    var parent = w.transientFor;
    var match = pattern.test(id) || (parent && pattern.test(idOf(parent)));
    if (match && !(w.desktops.length === 1 && w.desktops[0] === target)) {
        w.desktops = [target];
        print("d9-place: " + (id || "(senza classe, genitore " + idOf(parent) + ")") + " -> " + target.name);
    }
}
function watch(w) {
    place(w);
    if (w.windowClassChanged) { w.windowClassChanged.connect(function () { place(w); }); }
    if (w.desktopFileNameChanged) { w.desktopFileNameChanged.connect(function () { place(w); }); }
    if (w.transientForChanged) { w.transientForChanged.connect(function () { place(w); }); }
    print("d9-seen: [" + idOf(w) + "] caption=" + w.caption);
}
workspace.windowAdded.connect(watch);
print("d9-ready target=" + (target ? target.name : "NESSUNO"));
