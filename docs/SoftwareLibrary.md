# Software Library


**QUICK START TIP: use Inkscape to draw something with no fill. Export as a regular .svg (not inkscape svg)  Upload that to Krabscam.com build paths for the tools that you have and then Export as a .nc Gcode file which can up uploaded to your Maslow.**

This file is a place to share software recommendations that have been useful. All costs listed are estimates reported by contributors and have not been checked. Good opensource programs are listed first in each category otherwise no particular promotion is intended and all links have been submitted by community members.  

Maslow needs **Gcode** instructions to work.  There are many ways to generate those. Gcode is a human readable programming language that consists of a list of instructions that are sent to a machine to tell it how to move. They are mostly x,y,z coordinates and how to move between them. Instructions include curves, straight lines, and speeds. 3D printers and many other robots use gcode. 

You can open a Gcode file up in a text editor and edit it by hand if you need to.  You could cut parts out.  Only run half of the program, change distances and speeds all by typing in numbers in the Gcode code system.  It is not hard to learn the basics. (insert Gcode library link) 

THere are many many options to get to a sucessful Gcode for Maslow4 Some programs can do all of the steps, some can only do one or two. 


Generally a design will start in a program that lets you draw or specify the shapes for your design. You could use a **2D Vector drawing** program like Inkscape or Adobe Illustrator or a **three dimensional program** like Blender or Autodesk. 
Key words for these programs are **Vector drawing or Computer Aided Design CAD** programs. Files can be .**.dxf .svg .stl** but generally not .png or .jpg (bitmap pictures) 


Once you have a mathematcially defined shape, it needs to be translated into Gcode. The programs that can do this are called **Computer Aided Machining or CAM** programs. Examples are Krabzcam on the web, a wonderful and effective 2 dimensional free project. Autodesk and Autocad can do this too. In these programs you would describe the geometry of the router bit that you are using and the type of operation that you would like to do to the wood, like drilling or a profile cut or a shallow pocket cut and then the program designs a pathway for that particular router bit to leave the wood or material behind that you need as it cuts. The resulting file needs to be a **.nc Gcode file**.  


There are many good programs that can do both authoring and generate machine instructions often called **CAD CAM programs**. 


**Abundance** is a CAD CAM sister project of Maslow that aims to make a procedural genrated design program that can then output meaningful Gcode files all in the web. <https://abundance.maslowcnc.com/>  It is being developed now and is already fun and useful. 


We would like to add programs that people feel are useful maybe in weird ways.  For instance Pepkura Designer that is a specialized program for unfolding 3D polygon shapes into flat panels. It was designed for paper models but it is great for wood as well. Three D printers use programs called slicers that cut three dimensional shapes into a stack of 2D slices. Maslow could cut slices in wood or foam to be stacked. What other odd programs have you found useful in generating desgins to cut? 



# Useful external links
- Shapeoko wiki list of software <https://old.reddit.com/r/shapeoko/wiki/cam>


# SOFTWARE LIBRARY 
## To add a Program start a new entry with a title started by three ### hash symbols then add pictures, advice and description and links.  Still working on what is a useful format here, use your judgment. If we use the heading system built into markdown it will automatically create a table of contents in the top right corner of the reading pane. 


### Example Program entry heading text
PICTURE
- Overview:
- Links:
- Online, Program download or?:
- Cost:
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:

## ALL IN ONE CAD CAM 


### Abundance
<img width="600" alt="image" src="https://github.com/user-attachments/assets/42d79add-c997-4589-9ab5-6154c3f2b6c9" />

- Overview: Abundance is a sister project to Maslow. It is in the process of being developed by many of the same people working on Maslow4.  Abundance is built around the idea of procedural design.  Instead of drawing a table, in abundance you would program a table with variables and parts that could rebuilt and remixed. Leg length, top dimension, lumber size can all be variables that can be changed and the end design would be automatically changed as well. Abundance is being designed to work with Maslow as a free online full CAD CAM program so that you could design and then output cutting Gcode .nc files directly. (written 2025) 
- <https://abundance.maslowcnc.com/>
- Online
- Free 
- Experiences: A very different way of designing. It already can produce complex three d models and is a lot of fun to use. 
- Limitations: Still under development. Not a drawing program, a programming program. 
- Notes:
- - Build parts first as "molecules" then make a new project that puts those molecule pieces together.
- - Boolean operations make it easy to have pieces designed separately intersect and define cutting boundaries as you put them together. 
- More Pictures:
- <img width="600" alt="image" src="https://github.com/user-attachments/assets/37e9b15c-f8d7-4343-a4dc-1faf1bd257e5" />

- Credits: Barbour Smith

### FreeCAD
<img width="600" alt="image" src="https://github.com/user-attachments/assets/f552bbcb-6854-424b-b88e-dd916452c828" />

- Full CAD CAM parametric design software. Can do the entire process from design to Gcode. Well established project with pretty good support. 
- <https://www.freecad.org/>
- Download for Windows Mac and Linux
- Free Opensource project 
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:

  ### Easel
![Screenshot+2024-02-27+at+11 38 32 AM](https://github.com/user-attachments/assets/0557767a-780b-4867-b824-313bb154f8b6)

- Easel is a CAD CAM software. People like it for it’s easy to use interface.
- <https://easel.com/>
- Browser Based? 
- Cost: Free version can export gcode and $24/month version with more features
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:

## 2D Vector Design programs

### Inkscape
PICTURE<img width="600" alt="Screenshot from 2025-10-26 10-58-50" src="https://github.com/user-attachments/assets/4ec98e9a-0aee-45b4-9187-1475a10595ca" />

- Overview: Free Opensource go to program for Vector drawing. Inkscape is the well supported and developed opensource option for Vector drawing. It would be a great tool for designing a engraved sign or for drawing 2d cuts directly. 
- <https://inkscape.org/>
- Download the program and work on your own computer works on PC, Mac, and Linux. Not on phones or tablets. 
- Free Opensource project
- Experiences:
- - Excellent for 2d editing of .svg files
  - Lots of support videos and tutorials on the web.
  - Excellent for free graphic design.
  - For CNC gather the lines that you need to be cut in one operation and change them all to a set color.  Most CAM programs will read each color in a .svg file as a different operation. For instance Drilling can be green while profile cutting could be red.
  - Remember for CNC that we have to have the outside lines of shapes only , any lines in a .svg that are hidden under another piece of the drawing will cut. When finalizing your design be sure to Join all of the shapes together, turn off fill colors and check that the outlines are single, clear and non overlapping. 
- Details:
- Limitations:
- - Only 2 D, no good way to put pieces together in 3 D as you design. 
- Notes:
- More Pictures:
- Credits: inkscape.org


### Adobe Illustrator
PICTURE
- Widely used and well supported Vector design software. Part of the Adobe Creative Suite subscriptions. Lots of tools, professional sign designers use it. For CNC it would be good for designing signs, or editing .svg files
- <https://www.adobe.com/products/illustrator.html>
- Download program. Works on PC and Apple. Not phones or tablets or linux
- Cost: Annual subscription through Adobe. Can be free or reduced for students non professional use. If you are in school ask your art or design teachers. 
- Experiences:
- - Expensive but good.
  - Industry standard.
  - Many many features including AI design tools.
  - Need a good computer to run it.
  - Excellent for text and having fun with shaping text. 
  - Remember for CNC that we have to have the outside lines of shapes only , any lines in a .svg that are hidden under another piece of the drawing will cut. When finalizing your design be sure to Join all of the shapes together, turn off fill colors and check that the outlines are single, clear and non overlapping. 
- Details:
- Limitations:
- - Check your license to make sure you are using one that allows work for pay if you are designing for sales.  Educational licences have been limited. 
- Notes:
- More Pictures:
- Credits: Adobe

### Corel Draw

PICTURE
- Professional Vector graphics design software. Well established and supported. Lots of tools, professional sign designers use it. For CNC it would be good for designing signs, or editing .svg files
- <https://www.coreldraw.com/en/product/coreldraw/>
- Download program for Mac and PC Not phones tablets or Linux
- Cost:
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits: Corel
  
## 3D Design programs

### OnShape
![OnShape_007](https://github.com/user-attachments/assets/1b0772c7-5b77-48f6-b0d2-17fff7a35bb7)

- OnShape is a free 3D CAD software as long as you are fine with your 3D models being public. It has paid features and subscriptions. The interface is similar to SolidWorks. Used by product designers. It saves a version history as work is done. 
- <https://www.onshape.com/en/features/drawings>
- Online works in anything with a web browser. 
- Cost: Free and Subscription
- Experiences:
- Details:
- Limitations: Free is limited to public designs
- Notes:? Does it work better with a more powerful computer? 
- More Pictures:
- Credits:

### Blender
<img width="600" alt="Screenshot from 2025-10-20 17-34-38" src="https://github.com/user-attachments/assets/3a9ff011-b4db-4619-9cab-e56dfe608ff8" />

- Free Opensource project. Blender is a huge program that was designed to make Movies. It has a full set of three D design tools. It can do smooth sculpting as well as polygon models. It was not designed for making physical objects but it works well for this.  It is very powerful.  There are many tools that you can ignore for CNC design.  It is well supported and has lots of tutorials and videos to learn from. 
- <https://www.blender.org/>
- Download program for Apple, PC, and Linux Not phones or tablets.
- Free Open Source Project
- Experiences:
- - Focus on the tools that you need for your project.  It is easy to get overwhelmed by all of the tools and windows.
  - I really like blender for polygon design and for sculpting surfaces to be printed or cut.
  - It is a good program for students as they can run it on any computer platform and have free access to it. 
  - There is a plugin named Paper Unfold. It is amazing for making 2D cuttable pieces from a three D polygon model. It outputs as a .svg file that can be edited in inkscape. https://extensions.blender.org/add-ons/export-paper-model/ 
- Details:
- Limitations:
- - Not designed for physical product design but it has good tools that can be used for this.
  - More powerful computers work better. Save early save often. 
- Notes:
- - Example instructions for a CAD paper cutting machine. <https://www.instructables.com/Stuffed-Animal-or-Clothing-Manufacturing-with-CAD-/>
  - Example project using Blender to edit a three D model from Abundance <https://forums.maslowcnc.com/t/giant-useless-skull-out-of-my-neighbors-trash-boards/25165>
- More Pictures:
- Credits:

  ### Openscad
<img width="600" alt="image" src="https://github.com/user-attachments/assets/d8c8ba29-92c4-46af-ab59-811c792b8e99" />

- OpenSCAD is software for creating solid 3D CAD models. It is free software and available for Linux/UNIX, Windows and Mac OS X. Unlike most free software for creating 3D models (such as Blender) it does not focus on the artistic aspects of 3D modelling but instead on the CAD aspects. Thus it might be the application you are looking for when you are planning to create 3D models of machine parts but pretty sure is not what you are looking for when you are more interested in creating computer-animated movies.

OpenSCAD is not an interactive modeller. Instead it is something like a 3D-compiler that reads in a script file that describes the object and renders the 3D model from this script file. This gives you (the designer) full control over the modelling process and enables you to easily change any step in the modelling process or make designs that are defined by configurable parameters.

OpenSCAD provides two main modelling techniques: First there is constructive solid geometry (aka CSG) and second there is extrusion of 2D outlines. Autocad DXF files can be used as the data exchange format for such 2D outlines. In addition to 2D paths for extrusion it is also possible to read design parameters from DXF files. Besides DXF files OpenSCAD can read and create 3D models in the STL and OFF file formats.
- <https://openscad.org/>
- Free Download Linux, Mac, PC, BSD 
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:

### FlatFab
<img width="600" alt="image" src="https://github.com/user-attachments/assets/9d4c429e-fe2a-43f7-a547-c5d07268cea6" />

- Fun looking Free program for designing 2d cuts to 3D slot together forms. 
- <http://www.flatfab.com/>
- Download for Mac, PC, Linux 
- Free 
- Experiences:
- Details:
- - Outputs .svg files
- Limitations:
- Notes:
- - Video showing workflow https://www.youtube.com/watch?v=C_2dtZftMww
- More Pictures:
- Credits:

## CAM Gcode machine path 

### Krabzcam
<img width="600" alt="Screenshot from 2025-10-27 11-56-06" src="https://github.com/user-attachments/assets/9d806608-08aa-483a-9837-106304f843bc" />

- Great Free 2D Free Browser Based CAM.  Accepts .svg and exports .nc Gcode It is actively maintained and works well, however the number of options and settings can be overwhelming. It is a great free tool if you are getting started. 
- <https://mkrabset.github.io/krabzcam/krabzcam/index.html>
- Online browser based. 
- Free
- Experiences:
- - Be sure to read through the key hints in the background of the yellow window.
  - Save early save often.  It runs in your browser nothing is uploaded so make sure you are saving as you work. If your browser closes the project is gone. 
  
- Details:
- - Has options for loading png or jpg I haven't tried them. 
- Limitations:
- - 2D 
- Notes:
- More Pictures:
- Credits: Marius Krabset 


### Kiri:Moto
<img width="600" alt="image" src="https://github.com/user-attachments/assets/c48d3233-b2dd-436e-9147-a6b14695b51b" />

- Free Online CAM. Built for 3D printing but works well for CNC.  Works as a plugin to Onshape
- <https://grid.space/kiri/>
- Free Online in browser
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:

### UGS Universal G Code Sender
PICTURE
- Online project in Github to make a Gcode tool. looks useful
- https://github.com/winder/Universal-G-Code-Sender/wiki/Usage#designer
- Online, Program download or?:
- Cost:
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:

### HeeksCNC
<img width="600" alt="image" src="https://github.com/user-attachments/assets/7e52fdd1-ec2c-4e8b-b9e9-964c520e1fd7" />

- HeeksCNC is paid ($10 to own) CAM software which handles a large number of file types and can do 2D and 3D work. Windows only.
- <https://sites.google.com/site/heekscad/home>
- Download Windows only
- Cost: $10 (2025)
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:

### CamBam
<img width="256" height="221" alt="image" src="https://github.com/user-attachments/assets/ac7a669f-130a-463c-83d6-90b41726055f" />

- CamBam is a paid ($150 to own) CAM software. It does 2D and 3D work and can handle .dxf files.
- <http://www.cambam.info/>
- Download for Windows only
- Cost: $150 (2025)
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:

### CarbideCreate
![Screenshot+2024-02-21+at+8 24 43%20AM_005](https://github.com/user-attachments/assets/5bf6220d-9b5a-455c-aa3a-78d33df281d6)


- CarbideCreate is paid ($120/year) CAM software which works well for 2D and basic 3D work. CarbideCreate is one of the only options which works on Mac.
- <https://carbide3d.com/carbidecreate/>
- Dowload Windows and Mac
- Cost: $120 (2025)
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:



### Estlcam
![Screenshot+2024-02-27+at+11 50 07%20AM_008](https://github.com/user-attachments/assets/97369469-67bc-4419-be1a-7f03dca9a103)

-Estlcam is a paid ($59 to own) CAM software. The interface looks a bit more well thought out than others and the creator is active in our forums if you have any questions.
- <https://www.estlcam.de/>
- Windows Download
- Free with paid features 
- Experiences:
- Details:
- Limitations:
- Notes:
- - Tutorial video: <https://youtu.be/oQw0z3NS5lY>
- More Pictures:
- Credits:






## Other useful programs

### GCodeClean
- It’s a CLI tool (runs from a command line) to ‘clean’ 3-axis (XYZ) GCode files.
It will often reduce the file size, the number of GCode lines, and potentially the ‘G0’ travelling times.
It has 3 operations,

    - clean - which does most of the work
    - split - which will split a single GCode file into individual GCode files for each individual cutting path, and
    - merge - which can merge individual GCode files back into one file, and will try to reduce the travelling distance between the cutting paths.

As per the README.md in GitHub
A library and command line utility to do some ‘cleaning’ of a gcode (an .nc, .gcode) file. The primary objective is to be a GCode Linter, as part of that per line linting of gcode is already done.


We also have:

    - eliminating redundant lines (within tolerances),
    - converting very short arcs (G2, G3) to simple lines (G1), also within tolerances,
    - linear to arc deduplication, converting several simple lines to a single arc,
    - eliminate meaningless movement commands - especially G0 without any arguments,
    - correcting G1 to G0 when the z-axis is at a positive value,
    - clipping decimal places on arguments to meaningful values (as per the NIST spec),
    - per line linting: splitting lines to match the actual execution order as per the NIST gcode spec, and then reorganising the ‘words’ on a line to conform to some common practices (but not all),
    - annotate the GCode with explanatory comments (optional), note that annotating a cleaned file means that you may not be able to split the file later,
    - ‘soft’, ‘medium’, ‘hard’ or custom removal of superfluous tokens (minimise).
    - preamble linting: Adding a ‘standard’ set of gcode declarations, i.e. converting the ‘implicit’ to ‘explicit’.
    - postamble linting: Similar to the preamble, but at the end of the file (obviously).
    - file terminator matching: Ensuring that if the file demarcation character % is used at the start of the file then it is also used at the end.
    - splitting of GCode files into individual files, each with a single cutting path.
    - mergeing of previously split files, with some effort at ordering them to reduce the amount of travelling (G0) distance in total.

Forum members working on it - @md8n (LeeH), with a little update from @bar
- Linux, can be run on Windows and Mac
- <https://github.com/aersida/GCodeClean>
- Forum link about it: <https://forums.maslowcnc.com/t/and-the-travelling-salesman-arrives-at-gcodeclean-v1-3-0/19688>

### Pepakura Designer
PICTURE
- Very cool program designed for making paper models but just in general excellent for taking polygon 3D models and unfolding them into flat nets for cutting. 
- <https://pepakura.tamasoft.co.jp/pepakura_designer/>
- Only Windows PC
- Free download Have to pay for a license to export 
- Experiences:
- - Excelent program. Used with students in a middle and high school. Maslow with a drag knife would be super fun, Maslow with a pen could draw large scale projects. Could easily make panels with the router to make 3D models large in the real world. 
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:

### Donkektools Drag knife stuff
PICTURE
- Interesting looking paid software and free software from the makers of a drag knife.  On this list because it looked interesting and a different sort of tool that might give people a different way to use their Maslow. 
- <https://donektools.com/free-cnc-router-software/>
- Downloadable programs 
- Paid Core program and free other programs. 
- Experiences:
- Details:
- Limitations:
- Notes:
- More Pictures:
- Credits:



