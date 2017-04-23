# logoout-vdf

VirtualDub plugin to track a detected logo and remove it from a video

* Hardware and software needed

You need the following to get this program working :

\- Windows win32 compatible OS<br>
\- VirtualDub or VirtualDubMod<br>

* List of programs

\- \\plugins32\\logoout.vdf : plugin to copy into the VirtualDub folder<br>

* Original author

This code is inspired from the work of :

\- Shaun Faulds : RegionRemove<br>
\- Emiliano Ferrari : Blur Box<br>

* How to use it

Copy into the VirtualDub folder, launch VirtualDub, load a video.<br>
Add the 'logoout' filter and configure it, select 'Pass 1 : detect'.<br>
Select no video codec, select no sound, save the video to apply filter<br>

Configure the 'logoout' plugin again (yeah, VirtualDub's UI sucks)<br>
Select 'Pass 2 : edit', open the preview window, check the detection<br>
In the plugin window, select the boxes and tune the positions<br>

When you're done with editing, finally select 'Pass 3 : render'<br>
Select a video codec, select sound, save the video to apply filter<br>

* Some infos

While it is targeted to remove logos, it can also be used to remove faces.
