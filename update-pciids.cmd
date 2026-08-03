@set @rem=1 /*
@echo off
cscript %0 //E:JScript //Nologo
goto end
*/;

var SRC = "http://pci-ids.ucw.cz/v2.2/pci.ids";
var DEST = "pci.ids";

var XMLHTTP = new ActiveXObject("MSXML2.XMLHTTP");
XMLHTTP.open("GET", SRC, false);
XMLHTTP.send();

if (XMLHTTP.Status != 200) {
    WScript.Echo("update-pciids: HTTP " + XMLHTTP.Status + ": " + XMLHTTP.StatusText);
} else {
    var Stream = new ActiveXObject("ADODB.Stream");
    Stream.Type = 1;
    Stream.Open();
    Stream.Write(XMLHTTP.ResponseBody);
    Stream.SaveToFile(DEST, 2);
    Stream.Close();
    WScript.Echo("Done.");
}

/*
:end
::*/
