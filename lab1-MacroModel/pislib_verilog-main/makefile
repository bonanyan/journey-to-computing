
default: dpim

apim:
	iverilog -o wave src/APIM_tb.v
	vvp -n wave -lxt2

dpim:
	iverilog -o wave src/DPIM_tb.v
	vvp -n wave -lxt2

plot:
	gtkwave wave.vcd -S plt.tcl

push: 
	git add .
	git commit -m "update"
	git push -u origin main