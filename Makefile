CXXFLAGS=-w4 -e25 -zq -oabehikls -d0 -bt=dos -fo=.obj -mc

dos_objects = dos/tbdos.obj dos/aspiintf.obj dos/scsiintf.obj shared/toolbox.obj shared/scsishrd.obj
win_objects = tbwin.obj
win_resources = tbwin.res
dos_exe = scsitb.exe
win_exe = scsitbw.exe

%.obj: %.cpp
	wcl -c -cc++ -q $(CXXFLAGS) -fo=$@ $<

$(dos_exe): $(dos_objects)
	wcl -l=dos -q -lr -fe=$@ $(dos_objects)

$(win_exe) $(win_resources): $(win_objects) win\tbwin.rc $(dos_exe)
	wcl -l=windows -q -lr -fe=$^. -"option stub=$(dos_exe)" $(win_objects)
	wrc -q -bt=windows win\tbwin.rc $^.

clean:
	rm -f dos/*.err
	rm -f shared/*.err
	rm -f $(dos_exe)
	rm -f $(dos_objects)
	rm -f $(win_exe)
	rm -f $(win_objects) $(win_resources)

all: $(dos_exe)
