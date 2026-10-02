// Script KWin temporaneo (vedi tools/on-desktop9.sh): sposta sul «Desktop 9» le finestre dei test e di SyncView appena compaiono.
var target = workspace.desktops.filter(function (d) { return d.name === "Desktop 9"; })[0];
var pattern = /(^test_)|syncview/i;
function place(w) {
    if (!w || !target) { return; }
    var id = (w.resourceClass || "") + " " + (w.resourceName || "") + " " + (w.desktopFileName || "");
    if (pattern.test(id)) {
        w.desktops = [target];
        print("d9-place: " + id.trim() + " -> " + target.name);
    }
}
workspace.windowAdded.connect(place);
print("d9-ready target=" + (target ? target.name : "NESSUNO"));
