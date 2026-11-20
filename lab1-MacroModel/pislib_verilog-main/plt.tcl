set sigs [list]
lappend sigs "__bug_marker__"
lappend sigs "testbench.clk\[0\]"
lappend sigs "testbench.web\[0\]"
lappend sigs "testbench.cimeb\[0\]"
lappend sigs "testbench.d\[7:0\]"
lappend sigs "testbench.q\[7:0\]"
lappend sigs "testbench.cim_in0\[3:0\]"
lappend sigs "testbench.cim_out0\[5:0\]"
lappend sigs "testbench.cim_out0_correct\[13:8\]"

set added [ gtkwave::addSignalsFromList $sigs ]