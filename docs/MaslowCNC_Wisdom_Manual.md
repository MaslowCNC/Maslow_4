![LogoWithR](https://github.com/user-attachments/assets/66afe41d-6827-4a69-80c6-8bc330a144e1)

# INTRO
# Maslow-CNC-wisdom-manual for machines version 4 and 4.1 Current through 2025
Rough draft space for writing down instructions and useful tips for getting started on Maslow CNC currently for machine 4 and 4.1 This is intended as a comprehensive guide. A quick start manual could be made by linking to relevant sections.  If you are editing here is a guide to markdown typing commands https://www.markdownguide.org/cheat-sheet/ The general plan is that the topics are ordered by when you would need them in the process of getting started. To make sure we stayed consistent and throrough there will be parts that are duplicated from the build and setup instructions elsewhere in the manual.  It seems important to keep the basic build instructions and getting started info together and then have the sections later that expand and collect wisdom and specifics.    

<img width="2500" height="1875" alt="image" src="https://github.com/user-attachments/assets/ed9fcca8-0106-420d-b355-8df55d405ab9" />


# What is it? 
Maslow is a DIY 3 axis (x,y,z) computer numberical control (CNC) robot that controls a router to cut wood, plastic or other flat material.  It has been designed to work well on a 4x8 foot sheet of material or smaller and it is able to sculpt and cut vertically about 30 cm or 2.5 inches deep.  Maslow is designed around a small sled that carries the router and is anchored to an external frame by long belts that it uses to pull itself around.  This means that the core robot is very portable. It does require a stiff frame or anchors external to the robot.  It rides on top of the work being cut so it works well on cutting and sculpting shallow shapes but not giant bathtub sized hollows. Maslow is focused on accessiblility. 

 It is different from a gantry designed CNC router which are designed around a table that fits the material inside and then the robot moves forward and back on rails. It is much much less expensive than a gantry machine. It is in general less accurate than a good gantry machine. Gantry machines can also be 4 or 5 axis machines in which they can rotate the material in order to cut it at different angles. 

(wording in this section) 
Maslow is a project developed by Barbour Smith and a community of vollunteers and forum members as an open source project that is still under development.  It is portable, exciting, inexpensive and perhaps frustrating and still requires some troubleshooting and figuring out to get started.  Barbour has been working on iterations of this for more than a decade and many people have been successful at making many exciting projects including saunas, tables, signs, and a 30 foot catamaran! (link to project gallery) but it is not at the moment a perfect plug and play tool.  If making your own portable blade weilding robot in an enthusiastic online community sounds fun, then you are probably in the right place. 

<img width="2500" height="1667" alt="image" src="https://github.com/user-attachments/assets/d57a103a-4586-4fb7-8b92-709e616d6aef" />


## How does Maslow4 work?

Maslow4 uses four independently controlled steel reinforced belts to control its position while cutting. Each belt is controlled by a DC servo motor. The belt spools up around a central drum and passes between a pair of toothed rollers as it extends out. The roller which engages the belt’s teeth contains a high precision magnetic encoder which precisely measures the length of the belt allowing us to control the belt’s length to within 1/100th of a mm. Each spool has enough space to fully retract the belt for storage, and enough belt to cover a greater than 4’x'8’ work area when extended.

Each of Maslow’s 4 independent servo axes is driven by a powerful 24 volt motor coupled to a durable planetary gearbox which gives it the power that it needs to operate in either a vertical or horizontal orientation at speeds up to 2,500mm/min. Each servo axis has current feedback allowing the controller to detect when the belts are taut which is used during the automatic homing and calibration process.

The depth of the cut is controlled by two stepper motors with lead-screws which raise and lower the router as it moves. Maslow4 has 50mm or 1.9 inches of z-axis travel.

Maslow4 attaches to any flat rigid surface from horizontal surfaces like floors up to nearly vertical surfaces. It needs at least 15-20 degrees of angle off vertical to work correctly.


## How do I control Maslow4?

You can connect to Maslow4 from any Mac, Windows, Linux, i-OS, or Android device via WiFi, USB C, or Bluetooth. To connect via WiFi just connect to the “Maslow” WiFi network that the device creates when powered up and open any web browser (Firefox, Chrome, Safari, Edge…etc). You do not need to install any software. No internet connection is required, but if you have WiFi in your shop you can connect Maslow4 directly to your shop WiFi to control it from any computer on your shop network. Maslow4 uses industry standard g-code files so you can use it with any CAD/CAM software that you prefer (SolidWorks, OnShape, Fusion360, Autocad, FreeCAD, Ilustrator…etc). Maslow4 has enough internal memory to store hundreds of files, so if your computer goes to sleep, you close the window, or for some reason the connection is lost, Maslow4 will not be interrupted. 

If you live in a country with electrical outlets different from the ones in the US, you will need to find a cord which looks like this. The included power supply will work with 110-240 volts.
Maslow4 Technical spec:

Speed: 2,500mm/min (100ipm) x/y, 300mm/min (11ipm) z

Precision: +-0.5mm (1/64in)*

Work Area: 1.2x2.4 meters (4x8 feet) x/y, 65mm (2.6 inches) z** 

File type: GRBL standard G-code (.gcode, .nc) files

*Note that for Maslow type machines there are really two forms of precision that we care about. The first is repeatability…ie if run the same file twice how closely will the machine follow its past motions. The second is accuracy…because Maslow relies on a precision understanding of its anchor point’s positions if it is calibrated badly shapes can come out distorted. The automatic calibration process exists to address that issue. With both of these taken into account, we expect shapes cut with Maslow4 to be within +-.5mm of how they were drawn on the computer.

**Note that this is sort of an arbitrary statement. Maslow4 works within the bounds of its anchor points, and depending on the positioning of the anchor points a lot of different work areas are possible. For example 5’x5’ is also possible if you want a bigger square work area, or if Maslow4 is connected to the corners of a work bench then a smaller work area is possible. Generally the limiting factors are that the machine needs to be at least 1 foot from the anchor points to cut, and it can’t go so far from any anchor point that it runs out of belt.

### Maslow4 electrical requirements:

Electrical: 120-240 volts, 50-60Hz, <1 Amp*

*See note above for international plug compatibility
### Maslow4 space requirements:

Maslow4 can attach to any flat rigid surface so there isn’t a defined amount of space that it needs. When packed up for storage Maslow4 is 350mm (14in) across and 280mm (11in) tall. If you connect Maslow4 to an existing shop floor or work bench it won’t take up any additional space. If you would like to build the frame shown in the video, that frame is 12' feet, by 2’ by 7’ tall.




# History (I don't know a lot here) 
 In ???? (Barbour Smith and ?people?) designed the first Maslow as a wall mounted CNC routing robot that hung from two chains and pulled itself back and forth across a space. The robot at that point was designed around an ?arduino? microcontroller and was an open source design. Barbour set up the forums and github groups and sold ??? machines. It is still possible to make a Maslow Version 1, 2, or 3 and many people have been happy with them as useful tools. Makermade was a company that sold version ?2? under the open source license that is not affiliated directly with Maslow's developers. The current version Maslow 4 was prototyped as 3D printed parts with standard hardware. It has four machine belts instead of two chains. The parts are not interchangable with earlier versions. It uses a custom printed circuit board PCB built around an ESP microcontroller. The board also incorporates motor controllers. It is still possible to 3D print your own parts and replacement parts. It is still possible to design and program your own generic ESP microcontroller and use off the shelf motor controlers.  In ?2023? Barbour and ?? ran a succcesful Kickstarter campaign with which they used the proceeds to design and have injection molds made for injection molded parts and better compact custom PCBs. Injection molded parts are much stronger than most 3d printed parts.  This is what you are buying when you buy a Maslow kit. The custom PCB, the custom wires, the motors and custom made hardware, nuts and bolts,  and the injection molded plastics with fiberglass inclusions. 4.1 was the result of a second kickstarter campaign that upgraded the PCB, the nuts and bolts and other metal hardware as well as a better spool design. 

# Community members who have made significant contributions:
bar founder and primary developer

dlang ?Programming?

Ian_ab?

# How to get involved

# Future of the project

---

# MANUAL

# TABLE OF CONTENTS
1. Things you will need to get started
2. Optional but good things to have
3. Safety and hazards
4. Forums and github
5. Build instructions
6. Tips for building 4, 4.1
7. Details of optional upgrades
8. Routers lists and library
9. Router bits lists and library
10. CNC, CAD and CAM software lists and library
11. Material to cut lists and library
12. Frames and frame library
13. Connecting to the machine
14. Updating the Firmware
15. The Control panel and software
16. Other things you can control in the software.
17. Ancohors and calibration
18. Moving the robot around by jogging
19. G-code
20. Feed rates library
21. Test Cut designs and grids
22. Making a 2D design as a vector drawing .svg
23. Changing a 2D design into G-code .nc for cutting
24. Uploading G-code .nc to the machine
25. Fixing the work to the wasteboard
26. Inserting a cutting bit
27. Inserting a drawing pencil or pen (no idea how to do this)
28. Drawing with the machine
29. Preflight check
30. Cutting a 2D design
31. Designing a layered 2D design with mulitple depths
32. Preparing a layered 2D design as G-code
33. Cutting a layered 2D design with multiple depths
34. Switching cutting heads between operations
35. Designing a 3D sculpted surface design
36. Preparing a 3D sculpted surface design as g-code
37. Cutting a 3D sculpted surface design
38. Designing a 3D piece of furniture from multiple 2D panels
39. Abundance software
40. Paramaterized design for 3D
41. G-code for furniture
42. Cutting furniture.
43. 3D design as stacked layers
44. Cutting 3D as stacked layers
45. G-code for stacked layers of 3D designs
46. Other design methods
47. Including bushings and hardware in designs
48. Design library (perhaps one for parts of designs and one for full projects? )
49. Accuracy and precision
50. TROUBLESHOOTINg
51. Fun stuff

---

# Things you will need to get started
1.  **Maslow 4 or 4.1 kit** (link to store) Sometimes people also sell used ones on the forum.  As an open source project you could also print plastic and buy parts yourself. 
2.  **Router** The kit is built around the cylinder of the Dewalt dwp611 (or Dewalt D26200 in 240 volt) with its base removed.  It provides both the cutting power and the structural framework of the robot. It is possible to use other routers or spindles but you would need a shell to build it out to the dimensions of the dwp611.
3.  **Frame or anchors on a floor** to attach the belts to. A basic frame is roughly the size of a car parking spot or a bit bigger. Frames can be horizontal or up to 15 degrees from vertical so that the robot's weight pulls into the material. The current limit is anything less than 5 meters by 5 meters. It needs to be stable and not twist or warp  or lift when the Maslow pulls on the belts.  The frame also needs a replaceable waste board of material to support the projects as they are cut.  (link to frame design library)
4. **Cnc router bits** at minimum a 1/4 in Upcut spiral https://forums.maslowcnc.com/t/must-have-router-bits-for-maslow/19574/4
5. **CNC editing program**  There are many free or paid options.  Online there is Krabzcam, A free download is FreeCAD (check this), Any professional CAD/CAM software should work.  2D designs can be made in any vector drawing program such as Inkscape or Adobe Illustrator and then saved as an .svg.  3D designs can be made in Blender or a CAD program. The design then needs to be translated into instructions for the robot as a .nc G-code file.  Krabzcam or the other cad software can do this.  Cutting files are based on the type of material, the rpm of the router, the type and depth of cut needed, and the shape and size of the cnc router bit. Abundance is a parameterized CAD/CAM program in development that is part of the Maslow experiment that runs with a Github account. 
6. **Flat material** to cut. The machine was desgined for 4x8 foot sheets of plywood. It can work bigger or smaller. People have had success with plywood, oriented strand board OSB, particle board, plastics, and styrofoam, from 1mm to 30 mm thick (check)
7. **Wifi network or USBC cord.**  The machine is designed to connect to a local or home wifi network that you then also connect a computer or phone to open a local web page that controls the robot. It can be directly controlled with a USBC cord, or directly connected to your computer as its own wifi source. Updates are done by downloading the file to your computer from the Github repository and then uploaded to the Maslow locally.
8. **Torx 20?  Screwdriver**  One is generally included in the kit but more is good.

---

# Optional but good things to have
Many of these are available in the not shop on the Maslow website. 
1. 3D printed **button pushing insert** to make it much much easier to insert and remove router bits
2. **Dust control** vacuum system. Starting with a 3D printed nozzle adapter for the machine and then a hose (antistatic is good) and dust collection vacuum system.
3. **More CNC router bits** 1/8 in straight flute, Pointed engraving tip, rounded hollowing bit, things that make sense for your projects.
4. **Collet adapter** for 1/8 in shaft router bits
5. **Cord**, wire, and vacuum hose **control system** so that the connections don't pull on the machine too much.
6. **A way to hold the material in place while cutting**. Rug no slip fabric or double sided tape or brass screws or wood chocks or a vacuum system or....   Depends a lot on your material and setup. Don't use steel screws anywhere near your cutting area or the blades will colide with them.
7. **Metric meausring tape**
8. **Emergency Stop Button**  Extension cord with a clearly marked button for turning off power to the router and robot. 
9. **Handle** for moving the machine (?Best practice?

---

# Forums and GitHub
  Maslow is an open source project mainly guided by a few dedicated people but as an open source project it is also a community of enthusiasts who help each other, build new funcitons and could even fork the project to go in a new direction or a paralell development. 

  
  **When you make a Maslow you are not alone**, the **forums** are the places to look for wisdom, ask questions and ask for help, get and share project ideas propose and develop new functionality and celebrate successes. As a community it is imporant to be polite, to be gentle with criticism and mostly just help if you can. 

  
    You can read forums without an account but to comment or ask questions you will need an account. 
    Click on the M logo on the top left to get to the overall list of topics. It is good to search first for a thread to add to in case your question or comment is already under discussion.  If you don't see something relevant then you can make a new topic.  Try to tag the topic with categories to help others find it and understand what you are discussing. 

    
    (link to forum) 
    (list of main topics) 

    
    Click on your circle and name in the top right to see what activity has been connected with you and your discussions.  You can have a public topic, like and boost converstions with hearts, and send private messages with the mail funciton.  As you become more involved the automatic forum robot will send you messages giving you more priveleges, helpful training, and badges for forum actions as you figure them out. 


 **Github** is the technical knowledge repository where Maslow is stored and new versions and functions are developed. It is also where as a new user you will download the firmware files.  It is also where Abundance, Maslow's sister project of paramaterized CAD/CAM is being deveoped.  You can download the firmware files and manual there without an account but to participate or use Abundance you will need to create a github account. Github was designed for computer programming projects but can hold other things. Github has a resident AI called copilot primarily designed for computer coding which can help you to make contributions. 
    (links and instructions to find Maslow's stuff in Github) 

# Safety and hazards

    ---
    
# Building instructions for 4, 4.1
Find any of these steps confusing or get stuck? Don’t forget, you aren’t alone! Maslow is a community driven open source project. Ask in the [forums](https://forums.maslowcnc.com/) and we’ll figure it out together!

The Maslow4 interface is still evolving rapidly and will continue to change. This guide will be updated regularly, but you still might notice some small differences between the guide and what you see. 


Maslow4 is an open source project. Pull requests to change or improve the firmware are very very very much welcome and appreciated. If you don’t feel comfortable editing the software yourself you can still make a hugely meaningful contribution by telling us how it could be better in the [forums](https://forums.maslowcnc.com/). Sometimes the hardest part of making something better is figuring out what to change. If you can find a way to make the software simpler to use and better share it!

**This section is a Quick Start Software Guide followed by the build instrucitons**

## Quick start Software guide
This is to get you set up for the first time, there is more detailed information on each of these topics elsewhere in the manual. 

 ## This part of the guide will contain the following sections: 
 
1. How to connect to your Maslow4
2. How to update your firmware
3. How to connect Maslow4 to your home WiFi network (optional)
4. How to run the calibration process
5. How to move the machine around
6. How to define the machine's home position
7. How to run a g-code file
    
<img width="636" height="642" alt="image" src="https://github.com/user-attachments/assets/bc8b8ef8-a481-4b43-bfdb-3c76e09be5da" />

## How to connect to your Maslow4


Maslow4 is controlled using a built-in interface accessible from your web browser. You can connect to Maslow4 from any Windows, Mac, or Linux computer or iOS or Android tablet or phone. You do not need to install any software. 

Maslow4 will create a wifi network called **“maslow”** which you can connect to. The default password for this network will be **“12345678”**.

Connecting to the network will automatically open the user interface on most devices. If it does not you can type **192.168.0.1** into your web browser to open the interface. You may need to look up the exact numerical address for your machine in your home router admin settings.  


<img width="1000" height="554" alt="image" src="https://github.com/user-attachments/assets/54a3429f-e694-4c6b-ae67-47cde323115b" />

## How to update your firmware:

Maslow4’s firmware is improving regularly.


Luckily updating the Maslow4 firmware is easy. 


To update Maslow4’s firmware click on the FluidNC tab at the top of the screen, then click on the Update the Firmware button, and select your new firmware file.


You can always find the latest firmware version at 

https://github.com/BarbourSmith/FluidNC/releases

There will be 3 files that you need to download, firmware.bin, index.html.gz, and maslow.yaml. When you download the files, make sure your computer does not change their name. You must change the name back if this happens.


Note: When you first connect to Maslow it will create a popup to control the machine. On some devices you cannot upload files from within that popup (the window won’t open). The solution is to connect to Maslow from a regular browser window.


**Note that to update from a firmware version before 1.0 to a version after 1.0 you will need to use a USB cable. There is a video walkthrough for that process here.** https://youtu.be/od7DpdLel6A?si=xv1Zp3AIZFgRoeZ_


<img width="1000" height="559" alt="image" src="https://github.com/user-attachments/assets/62cc4555-b497-4030-acea-b0fff036329a" />


There are two other files which you will need to update periodically. These can be found by clicking on the FluidNC tab and then clicking on the files button.


<img width="1000" height="466" alt="image" src="https://github.com/user-attachments/assets/19b16055-8c78-4039-9791-4d9e68ec3fbf" />


This will show you your system files.


To upload a new file click the **Upload files** button at the top of the screen. If a file with the same name already exists it will be replaced.


**index.html.gz** controls how the machine interface looks. If you wanted for example a dark mode, replacing this file would give the interface a new look. I expect that there will be a number of community created UI options created quite quickly.


**maslow.yaml** contains the configuration settings for your machine. Your calibration values are stored here. You may not need to update the yaml each time you update the index and the firmware.


<img width="1000" height="493" alt="image" src="https://github.com/user-attachments/assets/c3eca3b6-95dd-489e-aa19-25930881e199" />


## How to connect Maslow4 to your home WiFi network

If you have an internet connection in your work space, it might be nice to connect Maslow4 to your existing WiFi network so your computer can stay connected to the internet and Maslow at the same time.


To connect your Maslow4 to the internet click on the **FluidNC** tab


<img width="1000" height="509" alt="image" src="https://github.com/user-attachments/assets/0707784f-985f-4055-8b9b-b4f10960b1b4" />




Then scroll down to enter your WiFi network name in the **Sta/SSID** field and your network password in the **Sta/Password field**, then press set next to each field. You will need to restart your machine for the changes to take effect. 


When your Maslow4 turns on it will attempt to connect to your WiFi network. If it is not able to find your network it will create a wifi network called “maslow” for you to connect to.


<img width="1000" height="536" alt="image" src="https://github.com/user-attachments/assets/0e4c67d3-5e18-4e86-8a88-52a8080b9f43" />



Once Maslow4 is connected to your wifi network you will need to find its address to connect to it. We were concerned about this working reliably on everyone’s networks so we have created three ways to do this so there is redundancy. 


The easiest way to connect to your Maslow4 on most modern wifi routers is to copy and enter the address **maslow.local** in your internet browser. 


<img width="1000" height="549" alt="image" src="https://github.com/user-attachments/assets/504a3129-f5cc-4b95-a804-980409b4fc6f" />


If that doesn't work, you can use the website findmymaslow.com to find the IP address of your machine. Unfortunately due to some (frankly prudent) security updates to Chrome this will only work in Firefox.


<img width="600" height="338" alt="image" src="https://github.com/user-attachments/assets/e963c899-7eb1-4e98-93a8-b1d24e87b9c7" />


Finally if all else fails, the blue LED on your Maslow4 will blink out the IP address so with a pen and paper you can count the flashes and write the number down. It’s not plan A, but it’s low tech and reliable. 


<img width="472" height="265" alt="image" src="https://github.com/user-attachments/assets/086357d4-a5f1-4e68-9f69-f137d7ad176b" />


## How to run calibration:

You only need to run the calibration process once when setting up a new frame. 


Before beginning the calibration process we recommend starting with the machine off and turning it on. This will ensure that we all start from the same place.


<iframe width="533" height="300" src="https://www.youtube.com/embed/mQwz0omOKJc" title="Maslow Calibration Walkthrough 1.05" frameborder="0" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" referrerpolicy="strict-origin-when-cross-origin" allowfullscreen></iframe>


These instructions can be somewhat confusing to follow so we made a video walk through of the current calibration process which you can also follow.


<img width="1000" height="1067" alt="image" src="https://github.com/user-attachments/assets/df7e3813-a2ff-4802-b6d4-a9a2a99f853f" />

First click on Alarm to unlock your machine

<img width="1000" height="847" alt="image" src="https://github.com/user-attachments/assets/0235efce-b74c-406c-b438-28ca38d94137" />

<img width="1000" height="750" alt="image" src="https://github.com/user-attachments/assets/3a47a416-ccde-408f-8a7b-50b8c763900c" />


Next, using the z-axis controls lower your z-axis all the way down until you hear it stop. Note that you want to remove any router bit before doing this.


<img width="1000" height="639" alt="image" src="https://github.com/user-attachments/assets/0d56b548-03ee-49ca-9665-3953c89a4b9f" />


Click Setup and then config


<img width="1000" height="543" alt="image" src="https://github.com/user-attachments/assets/73fb48a5-2d3b-40b2-9df3-af75ce4e0981" />


Make a rough measurement or guess at how far it is from your anchor points to the center of your frame in millimeters.


<img width="1000" height="1098" alt="image" src="https://github.com/user-attachments/assets/29610fc9-170d-475b-954d-01c246b8535c" />


Enter the distance you came up with into the “Extend Dist” field


Here you can also select your other calibration settings.


**Orientation**: Horizontal orientation is flat on the floor, vertical orientation is upright against the wall

**Calibration Grid Width / Height**: These parameters set the size of the calibration grid. The sled will move to these points during the calibration process so they should fit entirely inside the area that the sled can safely move within. Smaller grids are faster and easier, but larger is potentially more precise.

**Grid Size**: This is how many measurement points the machine will use to compute the calibration. More is slower but also potentially more accurate. 

**Retraction Force**: This is how hard the machine will pull when retracting the belts for storage. Increase this number if you are having trouble getting all of your belts to retract reliably. Turning it up too high can be hard on the machine.

**Calibration Force**: This is how hard the machine will pull when taking each measurement. This indirectly sets the tension in the machine’s belts while cutting. It seems like lower values might lead to more precise results, but it can’t be so low that the process doesn’t take accurate measurements.


<img width="1000" height="794" alt="image" src="https://github.com/user-attachments/assets/1a664a68-aeb0-48fd-aee9-54874415d122" />


Next click on Setup and then on Retract All. 


<img width="1000" height="750" alt="image" src="https://github.com/user-attachments/assets/6f73e5e9-eeeb-48d0-9757-2885c1bdb5f2" />


This will fully retract all four belts.


Note that if some of your belts don’t fully retract you can retract and extend them a few times to help loosen them up, or you can increase the amount of power used during retraction in the configuration settings.


<img width="1000" height="780" alt="image" src="https://github.com/user-attachments/assets/6b4a295f-da79-4afb-bd7d-48bdeb287bc9" />




Next press **Extend All** to begin extending the belts. You will hear the cooling fan turn on.


You want to keep extending all four belts until they stop. The machine will stop extending more belt once they have reached the target length.


<img width="600" height="1067" alt="image" src="https://github.com/user-attachments/assets/3c156185-5e35-476e-9bff-81f8a66af2f7" />


To extend the belts you will need to pull them out. A rocking motion will help get them started extending. You might need to be a little aggressive. After they will extend out to the correct length as long as you pull with a gentle but constant force. 


<img width="1000" height="750" alt="image" src="https://github.com/user-attachments/assets/2a5be063-d85e-4cf7-8d40-e8e78ba4352b" />


Attach all four belts to the frame. This might look different depending on your frame design.


<img width="1000" height="783" alt="image" src="https://github.com/user-attachments/assets/099e4067-7e70-48da-b469-4bd29a66d5ef" />


Press the calibrate button to begin the calibration process. The machine will move to each point in your grid taking a measurement at each one.


<img width="1000" height="551" alt="image" src="https://github.com/user-attachments/assets/12d294f4-d2c1-4dca-817d-e87344a92b6d" />


Multiple times during this process the machine will stop and recompute it’s anchor points. Be sure to keep your computer awake and connected during this process. The walk-through video above can give you a more clear understanding of what the machine is doing at each step.


<img width="1000" height="782" alt="image" src="https://github.com/user-attachments/assets/6f7deeb4-84e3-42db-812b-d5e85f6a9a58" />


That’s it, you are finished with the calibration process. From now on you do not need to do it again unless you change your frame’s dimensions. From now on if you disconnect your Maslow from the frame and want to re-attach it you can press **Retract All -> Extend All**, extend the belts and use the **Apply Tension** button to take up the extra slack.


<img width="1000" height="486" alt="image" src="https://github.com/user-attachments/assets/7e66623f-8a47-41ff-bee3-f03e30c14a4b" />


## How to move the machine around


You can move your Maslow around using the arrow buttons on the right side of the screen.


<img width="1000" height="754" alt="image" src="https://github.com/user-attachments/assets/f30cbb3b-bf63-4aaf-83f5-c208fb33b525" />


The distance that it will move can be set by clicking on a distance number.


There are separate distances for XY movements and for Z axis movements.


<img width="1000" height="856" alt="image" src="https://github.com/user-attachments/assets/c3a41655-8591-4aaa-84cb-6c0512f2aa2b" />


You can switch between mm and inches by clicking on the units.


<img width="1000" height="441" alt="image" src="https://github.com/user-attachments/assets/b708d8dc-fa8f-49d3-846b-83b3a4650b00" />


## How to define the machine’s home position


When you create a gcode file it will have a “home” position. Here you can see the home position shown with a cross in a circle. In this case when we created our gcode file we set it up so our shape would be cut above and to the left of the home position. The current position of the machine is shown with a purple dot.


<img width="1000" height="440" alt="image" src="https://github.com/user-attachments/assets/5f463bd4-317d-40e6-ab64-f3c5de447f39" />


If we want to move where the file will be cut on the sheet of plywood we can press the “**Define Home**” button which will move the file’s home position to the machine’s current position letting us move the shape we want to cut anywhere on the sheet.


<img width="1000" height="628" alt="image" src="https://github.com/user-attachments/assets/f1cf57bf-c6cd-49e2-9439-67f5eaa9ff10" />




Similarly we can set where the home position is for the z-axis. This will set where the router will start cutting which is typically on the top surface of the plywood. To set the home position for the z-axis click move the z-axis up or down with the **Up** and **Down** buttons. Then press **Define Home** to set the z-axis home position.


<img width="1000" height="856" alt="image" src="https://github.com/user-attachments/assets/d0058dc9-49f4-4599-a8c6-f84e0445c3d7" />


## How to run a gcode file


Load your gcode file onto the machine by pressing the **Load File** button


<img width="1000" height="1014" alt="image" src="https://github.com/user-attachments/assets/ba4e14ad-55bc-4f47-833b-21c4c35fd7b0" />


Select your file from the drop down


<img width="1000" height="1105" alt="image" src="https://github.com/user-attachments/assets/d58d5134-a5b6-4c48-82e8-71f09b5b2f17" />


Run the file by pressing the green play button.


<img width="1000" height="506" alt="image" src="https://github.com/user-attachments/assets/29649372-5061-4595-a448-90d8d3b8c3a5" />


## Troubleshooting


Any time you build a robot, there’s going to be some debugging involved. If you get any error messages you can find a full list of what they mean and what to do about them here.
https://www.maslowcnc.com/error-messages


We are also available to help in the forums!
https://forums.maslowcnc.com/

# End of the fast start software guide. 

---

# Maslow 4 parts then Maslow 4.1 parts

## What’s in the Maslow4 Kit?!


<img width="2500" height="1667" alt="image" src="https://github.com/user-attachments/assets/0522ec66-f20a-4850-a950-2f36577ae45c" />


Click links below to Download Printable pages. Thanks to our backer Craig for making these files.

https://www.maslowcnc.com/s/Whats-in-the-Kit_-Page-1.pdf
https://www.maslowcnc.com/s/Whats-in-the-Kit_-Page-2.pdf
https://www.maslowcnc.com/s/Whats-in-the-Kit_-Page-3.pdf
https://www.maslowcnc.com/s/Whats-in-the-Kit_-Page-4.pdf
https://www.maslowcnc.com/s/Whats-in-the-Kit_-Page-5.pdf


Five Axis Control Board. Quantity 1. Properties: Four servo axis, one stepper axis (dual drive), four ethernet encoder ports, two AUX ports, one fan control port. Wifi, bluetooth, USB C.
![DSC00166_002](https://github.com/user-attachments/assets/7a72ed8a-51d0-4be7-85f9-4f0ec5558969)


Magnetic Encoder Board. Quantity 4 Properties: AS5600 magnetic encoder with ethernet interface.
![DSC00168](https://github.com/user-attachments/assets/f9998bc8-dab8-401a-991c-305088c06bda)


Cooling Fan. Quantity 1. Properties: 60mm, brushless.
![DSC00157_008](https://github.com/user-attachments/assets/a2d650bd-a7d3-4dc7-a4da-40bc64da79c3)


Power Supply. Quantity 1. Properties: UL listed, 120-240 volts, 
![DSC00141_002](https://github.com/user-attachments/assets/ff85e52b-ebc0-49ae-82a5-6a438a39a233)


Standard NEMA computer cord plug, US plug cord provided.
![DSC00138_002](https://github.com/user-attachments/assets/40fb781f-f0b7-4fee-8f45-ec5b87fed560)



Ethernet Cable. Quantity 4. CAT5e, 310mm long.
![DSC00163_008](https://github.com/user-attachments/assets/a43d34f2-0826-46cb-89c4-2a6ad1c63e88)


PCB Enclosure Top. Quantity 1. Properties: Glass fiber reinforced polycarbonate.
![DSC00156_008](https://github.com/user-attachments/assets/26f2575b-0c56-4d0d-96a0-c27470f0fb38)


PCB Enclosure Bottom. Quantity 1. Properties: Polycarbonate.
![DSC00171_008](https://github.com/user-attachments/assets/40dc43a9-62a6-4674-9fac-d919b0457b52)

Stepper Motor. Quantity 2. Properties: 5mmx2mm pitch lead screw with POM nut.
![DSC00162_008](https://github.com/user-attachments/assets/e07d572b-17a2-4d1a-9311-3b38d6514c82)


DC Motor. Quantity 4. Properties: Planetary reduction gearbox.
![DSC00160_008](https://github.com/user-attachments/assets/8c4699b2-6800-4a6d-bf2d-38037c840131)


Drive Gear. Quantity 4. Properties: Steel, Fits D-shaped 8mm shaft with set screw.
![DSC00187_008](https://github.com/user-attachments/assets/8a96a58e-aeb4-4d60-a3c2-7f3539358cee)


Idler Gear. Quantity 4. Properties: Steel, Fits round 8mm shaft.
![DSC00185_008](https://github.com/user-attachments/assets/044f5f8a-9293-48ac-a5c7-fcfdf4620408)


Roller. Quantity 8. Properties: Glass fiber reinforced polycarbonate.
![DSC00195_008](https://github.com/user-attachments/assets/32bd0ec9-2fb8-4faa-ba96-f8e47f09d26d)


Belt End Half. Quantity 8. Properties: Glass fiber reinforced polycarbonate.
![DSC00181_008](https://github.com/user-attachments/assets/8cc61382-26b3-4027-b136-5a337e12b26e)

Belt Guard. Quantity 4. Properties: Polycarbonate.
![DSC00179_008](https://github.com/user-attachments/assets/4bf085df-cbe9-4bfa-ae29-0980a5ae43ee)

Inner Post. Quantity 8. Properties: Glass fiber reinforced polycarbonate. 
![DSC00173_008](https://github.com/user-attachments/assets/dc9d75f2-f97b-4daa-b4d7-6dfea70f06c5)
![DSC00174_008](https://github.com/user-attachments/assets/83b84e7c-cfe2-4546-8248-4e5bef97c912)


Linear Rod Clamp. Quantity 2. Properties: Glass fiber reinforced polycarbonate. Note: These will come preinstalled on the sled.
![DSC00178_008](https://github.com/user-attachments/assets/5cdc1889-e901-45ef-9063-5ad167e5fbba)


Support Riser. Quantity 2. Properties: Glass fiber reinforced polycarbonate. 
![DSC00148_002](https://github.com/user-attachments/assets/4f2d989f-54d0-4ab7-8cf3-a849300a8c8b)


Support Riser Top. Quantity 2. Properties: Glass fiber reinforced polycarbonate.
![DSC00176_008](https://github.com/user-attachments/assets/e062534b-24f9-416e-9fbf-edfb0b41e256)


Idler Shaft. Quantity 4. Properties: Hardened steel.
![DSC00196_008](https://github.com/user-attachments/assets/76eae2d1-76fc-418f-bd15-1515c668bf0b)


Linear Shaft. Quantity 2. Properties: Hardened steel.
![DSC00164_008](https://github.com/user-attachments/assets/b92540e6-35dd-46ed-ac73-5e86b5cfd88b)


Bearing. Quantity 20. Properties: Fits 8mm shaft.
![DSC00200_008](https://github.com/user-attachments/assets/2a9226b6-4507-4e7f-9e05-0de624d3287f)


Linear Bearings. Quantity 4. Properties: Fits 10mm linear shaft.
![DSC00189_008](https://github.com/user-attachments/assets/a446648f-d8f6-49b9-acbe-75d7bbe7311c)

Belt Spool. Quantity 4. Properties: Polycarbonate.
![DSC00154_008](https://github.com/user-attachments/assets/05bfba0a-81f6-4cf7-b78d-e496bdbb9db8)


Belt. Quantity 4. Properties: Steel reinforced TPU, 14.5 feet long.
![DSC00143_003](https://github.com/user-attachments/assets/d290f84b-8a47-43a3-9855-8170fd991a86)


Arm Frame. Quantity 8. Properties: POM.
![DSC00152_008](https://github.com/user-attachments/assets/224eb9ba-356d-4e87-88f7-0710b74ba83e)


Z-axis Clamp Wedge. Quantity 2. Properties: Glass fiber reinforced polycarbonate. Note that these will come preinstalled on the router clamp and really blend in.
![DSC00191_008](https://github.com/user-attachments/assets/d0db92b5-57a2-481c-a61d-10089f18a281)

Router Clamp. Quantity 2. Properties: Glass fiber reinforced polycarbonate.
![DSC00150_008](https://github.com/user-attachments/assets/5e96a625-66dc-49fe-8f3c-9e239e627abf)


Sled. Quantity 1. Properties: Glass fiber reinforced polycarbonate.
![DSC00209_008](https://github.com/user-attachments/assets/e9425ccb-5634-4312-8621-3e4d6b588928)


Dust Cover. Quantity 1. Properties: Laser cut acrylic.
![DSC00211_008](https://github.com/user-attachments/assets/a5f3364e-4f81-4ba6-8f17-d4a557b046a9)


M3x12mm Bolt. Quantity 175. Properties: Stainless steel, 2mm allen drive button head. 
![DSC00203_008](https://github.com/user-attachments/assets/0232ee0c-89e1-4af4-b035-bea20ab6e86f)


M3x6mm Bolt. Quantity 12. Properties: Stainless steel, 2mm allen drive button head. 
![DSC00215_008](https://github.com/user-attachments/assets/67495dad-95c3-4d4f-a2ae-a8ede0fb2d0f)

M3 Nut. Quantity 100. Properties: Stainless steel.
![DSC00206_008](https://github.com/user-attachments/assets/7605dd1d-2c76-44ca-aef4-8762651d4408)


M3 Locknut. Quantity 160. Properties: Stainless steel.
![DSC00208](https://github.com/user-attachments/assets/703fc7c8-6380-4537-a28c-67b256b80e97)


2mm Allen Wrench. Quantity 1. Properties: Stainless steel.
![DSC00183_008](https://github.com/user-attachments/assets/440c3ed6-d6c1-48f4-843b-f79aa58a4388)


Encoder Magnet. Quantity 4. Properties: Magnetic.
![DSC00202_008](https://github.com/user-attachments/assets/ee5cf25d-ed9b-4db3-b3b9-1db43ad2eadd)


Super Glue. Quantity 1. Properties: Gel
![DSC00145_008](https://github.com/user-attachments/assets/27ddd309-cac5-49ee-9ce9-d6f97e678466)

Thread Locker. Quantity 1. Properties: Removable thread locker. (No photo yet)


Silicone Grease. Quantity 1. Properties: Slippery. (No photo yet)

---

## What’s in the Maslow4.1 Kit?

Five Axis Control Board. Quantity 1. Properties: Four servo axis, one stepper axis (dual drive), four JST-XH encoder ports, two AUX ports, one fan control port. Wifi, bluetooth, USB C.
![IMG_5654](https://github.com/user-attachments/assets/0acd598d-b621-4bce-9d08-3af9379c24c5)


Magnetic Encoder Board. Quantity 4 Properties: AS5600 magnetic encoder with JST-XH interface.
![IMG_5656](https://github.com/user-attachments/assets/c48a00c1-3d80-4dc6-ab31-4e6176c85506)


Cooling Fan. Quantity 1. Properties: 60mm, brushless.
![DSC00157_007](https://github.com/user-attachments/assets/f3dd1a8b-1701-4a2e-b51c-221bc30d4cda)


Power Supply. Quantity 1. Properties: UL listed, 120-240 volts, Standard NEMA computer cord plug, US plug cord provided.
![DSC00138](https://github.com/user-attachments/assets/5468a914-fd45-4272-88e1-0ef8c0098a53)
![DSC00141_007](https://github.com/user-attachments/assets/0b602990-94e8-478a-9b99-5f942ff291aa)

JST-XH Encoder Cable. Quantity 4. 
![IMG_5651](https://github.com/user-attachments/assets/ee7d86d2-61dc-4a4e-b16e-701eec8a43ad)


PCB Enclosure Top. Quantity 1. Properties: Glass fiber reinforced polycarbonate.
![DSC00156_004](https://github.com/user-attachments/assets/1e3d145b-beed-49d1-9097-af4d17326757)


PCB Enclosure Bottom. Quantity 1. Properties: Polycarbonate.
![DSC00171_007](https://github.com/user-attachments/assets/69ded651-6fc7-47a9-b7f6-d28c72f08a10)

Stepper Motor. Quantity 2. Properties: 5mmx2mm pitch lead screw with POM nut.
![DSC00162_006](https://github.com/user-attachments/assets/8396aafd-e441-4cb5-9a8b-e2619ea4914d)


DC Motor. Quantity 4. Properties: Planetary reduction gearbox.
![DSC00160](https://github.com/user-attachments/assets/e498f868-c3a0-4ee7-afe3-06dffab9a098)


Drive Gear. Quantity 4. Properties: Steel, Fits D-shaped 8mm shaft with set screw.
![DSC00187](https://github.com/user-attachments/assets/ec146db7-ebcc-447c-af87-e45059e934f8)


Idler Gear. Quantity 4. Properties: Steel, Fits  8mm bearings.
![IMG_7371](https://github.com/user-attachments/assets/b213efe4-fed9-4a39-9ad5-f7a35b9f33e4)


Roller. Quantity 8. Properties: Glass fiber reinforced polycarbonate.
![DSC00195_003](https://github.com/user-attachments/assets/bff09b64-e637-4298-bd80-121ec2dd15a2)


Belt End Half. Quantity 8. Properties: Glass fiber reinforced polycarbonate.
![DSC00181](https://github.com/user-attachments/assets/346f4aaa-a866-4b0e-9fec-c707f976e3d8)

Belt Guard. Quantity 4. Properties: Polycarbonate.
![IMG_7358](https://github.com/user-attachments/assets/781d5814-e39f-4094-88ce-be9e0197ac06)

Inner Post. Quantity 8. Properties: Glass fiber reinforced polycarbonate. 
![DSC00173](https://github.com/user-attachments/assets/68ed361e-f24c-410f-8316-432580d52e5f)
![DSC00174_005](https://github.com/user-attachments/assets/538bf3c1-72df-4797-8b08-6175ff36467c)


Linear Rod Clamp. Quantity 2. Properties: Glass fiber reinforced polycarbonate. Note: These will come preinstalled on the sled and may be hard to spot.
![DSC00178](https://github.com/user-attachments/assets/2f250e8c-ce2c-4d43-937c-4a8ea23b3d2d)


Support Riser. Quantity 2. Properties: Glass fiber reinforced polycarbonate. 
![DSC00148_006](https://github.com/user-attachments/assets/b640e6a9-86ff-4a70-a709-e698d653d716)


Support Riser Top. Quantity 2. Properties: Glass fiber reinforced polycarbonate.
![DSC00176_004](https://github.com/user-attachments/assets/95e0e743-f89b-4554-aa40-93b5f8403187)

Linear Shaft. Quantity 2. Properties: Hardened steel.
![DSC00164_004](https://github.com/user-attachments/assets/0ef0f023-724f-4faa-bd18-0eaef1594b72)

Bearing. Quantity 30. Properties: Fits 8mm shaft, 148ZZ is the common designation. 
![DSC00200](https://github.com/user-attachments/assets/6b206378-f3c4-4366-b106-b5e39bf0d6f1)


Linear Bearings. Quantity 4. Properties: Fits 10mm linear shaft.
![DSC00189](https://github.com/user-attachments/assets/c041756f-0dfb-44c5-a4c1-5698f1dae83b)



Belt Spool. Quantity 4. Properties: Polycarbonate.
![DSC00154_004](https://github.com/user-attachments/assets/2a880cf6-a76b-4ea0-ac48-3568af18ddbe)


Belt. Quantity 4. Properties: Steel reinforced TPU, 14.5 feet long.
![DSC00143_008](https://github.com/user-attachments/assets/0dc790d4-0cda-4316-b6b4-89481f2e5777)


Arm Frame. Quantity 8. Properties: POM.
![IMG_7363](https://github.com/user-attachments/assets/e253a8fa-9501-40e9-ae28-db442154832f)


Z-axis Clamp Wedge. Quantity 2. Properties: Glass fiber reinforced polycarbonate. Note that these will come preinstalled on the router clamp and really blend in.
![DSC00191](https://github.com/user-attachments/assets/efc4a520-3edb-4f4a-a04e-47bc77c19d94)

Router Clamp. Quantity 2. Properties: Glass fiber reinforced polycarbonate.
![DSC00150_002](https://github.com/user-attachments/assets/32d40a77-0406-4c9e-a5e6-f3d66d75d962)


Sled. Quantity 1. Properties: Glass fiber reinforced polycarbonate.
![DSC00209_003](https://github.com/user-attachments/assets/7584ce35-5e44-466f-961a-0cdfd7b4a3a0)


Dust Cover. Quantity 1. Properties: Laser cut acrylic.
![DSC00211_002](https://github.com/user-attachments/assets/c0fb7b66-035d-4378-83a7-ab10edb0f0d9)


M3x12mm Bolt. Quantity 150. Properties: Black oxide steel, 2mm torx drive button head. 
![F5C182B1-9B6B-450B-87A6-F663FDC3C1E7](https://github.com/user-attachments/assets/c5a130b9-67aa-4938-b756-f07cda77e8d3)

M3x12mm Bolt with Loctite. Quantity 20. Properties: Black oxide steel, 2mm torx drive button head.
![AEE79059-F043-4095-B7D1-8394BD85048F_008](https://github.com/user-attachments/assets/44422998-5dc1-483a-90b9-1147377f5cef)

M3x6mm Bolt. Quantity 12. Properties: Black oxide steel, torx drive button head. 
![90E1DD68-72A5-49A2-B337-B99131CA1ECD](https://github.com/user-attachments/assets/929848df-e477-491e-85ce-a886082abe2e)

M3 Nut. Quantity 30. Properties: Stainless steel.
![DSC00206](https://github.com/user-attachments/assets/1b02e6e1-b05f-4919-a692-c179c988d229)

M3 Locknut. Quantity 150. Properties: Stainless steel.
![DSC00208_002](https://github.com/user-attachments/assets/b88650ce-e1dd-413e-ac7b-31a593d3e277)


T8 Torx Wrench. Quantity 1. Properties: Stainless steel.
![03CAB142-539C-4F23-9D0B-D1F9354CCF55](https://github.com/user-attachments/assets/42b77165-d530-47f6-ab28-4d243e82a8bb)

T10 Torx Wrench. Quantity 1. Properties: Stainless steel.
![C381A4C3-4B7D-4DD8-892D-F8F277324CA2](https://github.com/user-attachments/assets/00f967c5-f64a-4072-8219-e6be3bcc1f9a)


T10 Torx Driver. Quantity 1.
![47811757-CD7B-4B3C-9B88-13B2481E961F_008](https://github.com/user-attachments/assets/d88f25f2-7f81-4d00-8f53-b1b937b6c815)

Encoder Magnet. Quantity 4. Properties: Magnetic.
![DSC00202](https://github.com/user-attachments/assets/11288eaf-349c-43f9-8c50-7db123a79b81)


Super Glue. Quantity 1. Properties: Gel
![DSC00145_004](https://github.com/user-attachments/assets/27e5028f-d6cb-4a11-b934-8b675a78265d)

---


 ## Building the Maslow4 Frame


### Basic Guidelines:

Maslow4 is intentionally versatile. You can attach Maslow4 to any flat rigid surface with at least fifteen degrees of angle to it and so there is no “best” way to set your machine up. 

### Any flat rigid surface:

    Maslow4 needs four anchor points on any flat rigid surface. How flat? Reasonably flat. Maslow4 depends on the surface that it is sliding on to be flat so any bending in the surface that it is resting on will result in imperfect cuts. That being said the average garage floor or reasonably straight 2x4 is plenty flat.


How rigid? Maslow4 can pull with up to about 40lbs of force on it’s anchor points so it is important that the surface that Maslow4 is connected to not flex under that force. This is more important in the vertical configuration where the stresses due to gravity are quite different at the top of the sheet than at the bottom of the sheet. In the horizontal configuration the forces are more similar everywhere.


### Horizontal vs Vertical: 

Maslow4 will work at any angle from horizontal up to about 20 degrees from vertical. Maslow4 was originally designed for the vertical orientation and the fact that it will work horizontally was more of an afterthought, but we’ve really started to enjoy using it in the horizontal orientation too. One option is not better than the other, whichever one fits your work space better 


### How do the belts attach?

Each of Maslow4’s belts terminate with a belt end ring which can be attached to an anchor point. The hole in the end of this part is 10mm or 3/8ths inches and can attach to a 10mm or 3/8ths inch bolt. It’s preferable if the bolt is smooth, but it will still work if the bolt is threaded.


![4700724E-439F-451D-B10E-B73C26CB7475](https://github.com/user-attachments/assets/f51ac40d-ca10-4ba8-afd4-f1f317239797)


The end of the belt can be connected to a bolt, a shoulder bolt, or a quick release pin.
![6E78B686-7B2F-4D6C-9158-38F5AF91A4A1_002](https://github.com/user-attachments/assets/7545138f-f6b5-406b-9e36-22695beeb4d9)

### Safety:

When attaching Maslow4 to a surface it’s important to ask “How bad would it be if I were to cut through the thing that I am cutting and hit this surface. If the answer is “Very bad” then it’s not a great surface to cut on. Generally there is a “spoil board” under the piece of wood being cut which protects the underlying surface, but mistakes can happen.


## Attaching Maslow4 Directly to the Floor


 If your work space has a concrete floor that you can attach things to, consider attaching Maslow4 directly to that surface. This is the simplest and cheapest option, while at the same time taking up the least amount of work space when the machine is not in use. 


The very simplest cheapest, and strongest, version of this is to use 3/8ths inch drop in anchors which mount directly to the concrete itself.


Then when the machine is not in use it has almost no footprint and when it is in use you have the most rigid and durable frame possible. We used these anchors, but there are many comparable options out there. https://www.grainger.com/product/DEWALT-Expansion-Anchor-3-8-16-Thread-30RZ53
![1000012945_008](https://github.com/user-attachments/assets/bd1461b1-bfcf-4ec9-89a3-a0317db7b90c)
![1000012945](https://github.com/user-attachments/assets/1899cda7-c5aa-4b80-84ea-34b7d9ca34ae)


If you cannot drill into the floor itself you can use 3D printed anchor points which can be glued to the floor. These also work well, but leave a tripping hazard when the machine is not in use.
![image1](https://github.com/user-attachments/assets/e0589149-89fd-44c6-9289-f611140b4e05)
![image2](https://github.com/user-attachments/assets/ba12bc84-1c87-48d9-98d5-40c945fe680a)
![IMG_1728_003](https://github.com/user-attachments/assets/6e19b620-3774-48eb-a470-f64efa82c471)


In this case I put my anchor points the long way in the garage, but if I were to do it again I would put them the other way so that they would be all the way by the walls so I wouldn’t need to step over them.


You want to place the anchor points on the floor in a roughly rectangular shape 8’x12’ (2.5x3.5m). Consider running the anchors horizontally across your space so that the anchors can be tucked back against the walls or under work benches where they won’t be under foot.


This design uses these 3D printed anchor points. If you have a 3D printer you can print your own for about $2 each, or if you don’t have a 3D printer you can take the .STL files and get them printed by craftcloud3d.com for around $7.50 each. This 3D printed part is designed to use this quick release pin, but any 10mm pin will work.
https://www.maslowcnc.com/s/Floor-Mount-Quick-Release-Anchor-Point.stl
https://craftcloud3d.com/
https://www.amazon.com/gp/product/B0BWYLVNWS


We’re working on getting kits of these parts for sale in our store.


There are more options for frames depending on your situation and space.  These are listed and documented in the Frame Library Section. 

---

## Basic Wooden Frame Directions

![90797204-6F40-4B60-8F33-F2EB10633B80_008](https://github.com/user-attachments/assets/46ac4974-9af0-47ee-8af3-77a58d01cf9f)

If attaching Maslow4 to your floor is not an option, here are the plans for the basic frame that we have been using which has worked well for us. This frame is made from 2x4 lumber which is pretty universally available and affordable. We’re going to show plans for the most basic version of this frame, and then a couple optional upgrades if you want. The materials are listed for US sizes, but very comparable materials exist in metric sizes (ie 18mm plywood instead of 3/4).


### Material List

    - 2 count 10 ft 2x4s ($12.68 at Home Depot - They don’t seem to have fir, but it would be fine)
    - 5 count 8 ft 2x4s ($3.28 at Home Depot)
    - 1 box of 1 and 5/8ths inch drywall screws ($7.58 at Home Depot)
    - 2 count sheets of ¾ inch plywood 
    - 4 count  3/8ths by 6” bolts ($1.43 at Home Depot)
    - 4 count 3/8ths Tee Nuts
    - 4 count 3/8ths Wing Nuts ($1.38 at Home Depot)
![899F88F4-C47C-4E2C-88D3-2D28D01D6D31_003](https://github.com/user-attachments/assets/ba0ce27f-4f09-4441-8fcb-ad2d7a4f27a2)
![42F282D7-5E6A-47E4-BA33-A9E156E9D1E6_008](https://github.com/user-attachments/assets/d4e645bd-07dd-4584-8428-912166a663cd)


### Tools

    - Drill (With 3/8ths drill bit and fillips screw bit)
    - Saw (Anything that can cut plywood)
    - Carpenters Square
    - Tape Measure

### Instructions

1 - Using a saw cut 8x of the corner plates and 12x of the inside plates


Click here for printable templates for the Corner Plate (metric version)
[https://www.maslowcnc.com/s/Corner-Plate-Metric.pdf](url)
and the Inside Plate (metric version) 
[https://www.maslowcnc.com/s/Inside-Plate-to-Scale.pdf ](url). 
Print this on 8 1/2” by 11” paper or A4 Paper. The exact shape and dimensions of these parts absolutely do not matter. You can cut them by hand without measuring anything if you would like. Note that the circles on these parts are the approximate screw locations and do not need to be drilled.
![42F282D7-5E6A-47E4-BA33-A9E156E9D1E6_008](https://github.com/user-attachments/assets/a1b76199-edff-4235-b205-98e52f44822f)

2 - Stack the corner parts in groups of two and drill a 3/8ths hole in each stack two inches from each edge
![Screenshot+2024-01-09+at+4 15 02+PM](https://github.com/user-attachments/assets/e31538b6-f252-4253-b94c-f25949aab4ce)

3 - Insert a 3-8ths inch tee nut into one of each pair of corner plates
![Screenshot+2024-01-09+at+4 20 55+PM_007](https://github.com/user-attachments/assets/72af1e21-e2fa-4ee9-80a1-b769833b9a7f)
![Screenshot+2024-01-09+at+4 21 10+PM_006](https://github.com/user-attachments/assets/2461b7f3-8877-4562-8894-02bb4e4b0a0d)

4 - On a flat surface, layout the two 10 ft 2x4s parallel to each other roughly 8 ft apart. The 2 inch sides should be facing up.


Place one of the corner plates with the tee nut installed under each end and then place an 8’ 2x4 between them on each side.
![Screenshot+2024-01-09+at+3 56 02+PM](https://github.com/user-attachments/assets/d479ac03-7d42-4f6c-8c19-1a824290760a)


5 - Layout the five 8 ft 2x4s between the 10 ft pieces perpendicularly. The 2 in sides should  be facing up. There should be one at either end to create a rectangle and three evenly spaced in the middle.


The exact spacing of these braces is not critical. Here is an illustration of the recommended spacing, but if your lumber is a slightly different length do not worry. What we are going for is a roughly even spacing.
![Maslow+Frame+Drawing_002](https://github.com/user-attachments/assets/839d5f2c-12e0-46d2-a56e-560e8e5b544f)

open this drawing (imperial) https://www.maslowcnc.com/s/Maslow-Frame-Drawing.png

open this drawing (metric) https://www.maslowcnc.com/s/Maslow-Frame-Metric.pdf

6 - Using a carpenter's square to make sure the corners are roughly 90 degrees, screw one Corner Plate to each corner of the frame. Use 2 screws for each 2x4 following the layout shown in Detail A. Each plate receives 4 screws
![PXL_20231220_233225139_003](https://github.com/user-attachments/assets/275e1834-2747-4a44-81c4-3d281440b2a7)

7 - Using a carpenter's square to make sure the 2x4s are roughly 90 degrees, screw one Inside Plate to each intersection of 8ft and 10ft 2x4s. Use 2 screws for each 2x4 following the layout shown in Detail A. Each plate receives 4 screws
![PXL_20231220_233231011_003](https://github.com/user-attachments/assets/ff2dd26f-86e4-46e1-bd02-e63576f83f0b)

8 - Flip the frame over so that the newly attached plate are facing down


* Tip: Please ask someone to help with this part. This thing is big and awkward to move.

![5E9E41D2-EF00-4EDB-9C93-863DAC9E70FD_002](https://github.com/user-attachments/assets/4f32f827-9a4d-45e7-8f16-3b068570df0f)


9 - Screw one Corner Plate to each corner of the frame. Use 2 screws for each 2x4 following the layout shown in Detail A. Each plate receives 4 screws

10 - Screw one Inside Plate to each intersection of 8ft and 10ft 2x4s. Use 2 screws for each 2x4 following the layout shown in Detail A. Each plate receives 4 screws

11 - Center the remaining piece of ¾” plywood in the frame to create a spoil board..
![90797204-6F40-4B60-8F33-F2EB10633B80_003](https://github.com/user-attachments/assets/e85d43c1-2395-4314-a97d-7a2d151395f0)

12 - Screw the plywood to the 2x4s underneath using 2 screws per 2x4, one on either side of the plywood board. There will be 6 screws in the spoil board in total. 
![PXL_20231220_234115425 MP_008](https://github.com/user-attachments/assets/3c259a60-8f30-4fc6-b719-81c525b73632)

13 - Thread the 3/8ths inch bolts through the frame and into the tee nut. Then flip the frame over and thread the bolts all the way through so that they protrude.
![E080EB3F-7F0D-49AF-A43D-6353258A6639_008](https://github.com/user-attachments/assets/30b094bb-65b1-47d0-bb58-d3a4d475a6b4)

These will server as the anchor points for the machine. 
<img width="1000" height="750" alt="image" src="https://github.com/user-attachments/assets/6483a286-f85b-4eac-bee5-41d06fed5f71" />

14 - Done! Modify as needed.

This is a rough guide to make a frame. If you have room you can use 12’ 2x4s to get even better performance or you can make your frame smaller if needed. Maslow4 is designed to work with any flat rigid surface so a lot of variation is possible.



**Optional Upgrades**

- A 3D printed belt end anchor for bolts or pins can be screwed to the corner plates
- You can find a 3d printable file for our quick release anchor point on the maslow website.
- Add cross bracing with a gate kit to increase frame rigidity
- Suspend the frame on hooks or pulleys for storage

---

## Maslow 4 Assembly Guide

Maslow4 is designed to be assembled in a weekend using only the provided Allen or Torx wrench. 

![03CAB142-539C-4F23-9D0B-D1F9354CCF55](https://github.com/user-attachments/assets/45d0d87e-b4fb-4823-b72b-147b798bad8a)

The assembly process is broken down into four steps. The assembly process is very similar for Maslow 4 and 4.1, but since some parts look slightly different we have created two sets of instructions. You can find step by step instructions and a video for each part here:


    1. Background and Context: 4.0 and 4.1
    2. Assembling the arms: 4.0 and 4.1
    3. Assembling the sled: 4.0 and 4.1
    4. Assembling the router: 4.0 and 4.1
    5. Putting it all together: 4.0 and 4.1

4.0 instructions will be first.  Scroll down to find all of the 4.1 instructions together. 
![image1](https://github.com/user-attachments/assets/41eff5ab-5708-4eb3-a01c-617cde5b8358)

## 4.0 Assembly Instructons Background and context

![image5](https://github.com/user-attachments/assets/5e51b546-eb98-4740-aaa2-f82ca993446c)


The Maslow4 design has evolved significantly since we launched the Kickstarter. We are proud of every change and improvement that we’ve made. Maslow4 is a substantially better machine than what we initially promised as a result…but the changes have led to some chaos in the hardware bag.

![IMG_3266_003](https://github.com/user-attachments/assets/62f0ca0f-a039-4d8d-95c5-75159b506a31)

Maslow4 includes three hardware bags each of which reflects the changing design needing more and different hardware. 


Initially Maslow4 used regular nuts, but we decided to offer locking nuts as well. Unless the type of nut is specified feel free to use either. 

![image2_002](https://github.com/user-attachments/assets/edb33a8b-e285-4fe7-b097-8c6d7fb9a5be)

Wherever possible Maslow4 is designed to be put together with a single length of bolt (the versatile M3x12mm) to make assembly and maintenance easy. For the most part we have succeeded in that, however there are two places where a different length of bolt are needed. The shorter M3x6 are used to attach the stepper motors due to space constraints, while the longer M3x16 are used to attach the cooling fan due to a miscommunication with the fan manufacturer. We’ll get that one removed down the road.

![image3_003](https://github.com/user-attachments/assets/a8e8208e-37ed-4c1a-beec-12ef18f5c425)



We have succeeded in making it so that you only need one tool (the included 2mm allen wrench) to assemble the entire machine, although a blunt object like a hammer is helpful to GENTLY coax some of the stiffer parts into place.


Wherever possible we’ve tried to make it so that things can only go together in one clear way.


![image4](https://github.com/user-attachments/assets/b31df1ea-e4c9-43db-9eef-cfc9b5f9e3ad)

If at any point we’ve failed to make a step in the instructions sufficiently clear let us know in the forums! If something was unclear to you it’s almost certainly unclear to other folks too. Your feedback helps to improve the assembly process for everyone.


We would be especially interested to hear how difficult your found the process and how long it took. What age of kids do you think that Maslow4 would be an appropriate project for with parent supervision?


Finally, if you get stuck on something or have a question, the forums are also the best place to get a quick answer. The sum total of the Maslow community is astounding.


## Assembling the Arms

![IMG_3168+2](https://github.com/user-attachments/assets/d1e6074e-8fff-4c03-a17b-09e5689d87f2)


Maslow4 has a few parts that need to be glued so we will start there so that the glue can begin drying. In all of the cases where we use glue, the glue is not structural, it is just meant to hold parts in place that might move due to vibration. Use just a small amount of glue.


Assembling the arms is the most complex and difficult part of the Maslow4 build process so we’re going to jump right in and start there.


Find any of these steps confusing or get stuck? Don’t forget, you aren’t alone! Maslow is a community driven open source project. Ask in the forums and we’ll figure it out together!

![image23](https://github.com/user-attachments/assets/ad0b4024-9f9a-41af-8e6d-09458562ef55)


First we will gather the four belts, the four arms, and the super glue which we will need for this step. The top and bottom halves of the arms are identical.


![image31_007](https://github.com/user-attachments/assets/6526849e-f29a-456a-8a21-9a194ba139b6)

Removing the spool from each arm, place a small drop of super glue in the slot where the belt will attach like this.


A couple notes on this section. The super glue is opened by twisting the top. One end of each belt might fit into the spool more easily than the other so it’s worth checking both ends before starting with the glue.

![image6_004](https://github.com/user-attachments/assets/1225e54c-0b20-4c3d-ad73-c81051127be1)


You do not need to use much glue. A very small drop is fine. 


![image17](https://github.com/user-attachments/assets/0e738994-90c4-43bc-98e2-f836f9c3315f)

Then press the belt fully into the slot. It should not stick above the top of the slot. 

![image2](https://github.com/user-attachments/assets/87513fab-8019-42fb-a010-6bea9e828287)

Repeat this step for all four spools.

![image14_007](https://github.com/user-attachments/assets/bcb15253-09b0-49b0-ab2b-cbac5efbb82a)


Next we will glue the encoder magnets into the rollers. For this step we will need to gather the eight rollers and the four magnets.

![image12_003](https://github.com/user-attachments/assets/2b5be5e1-da93-4b2e-92b2-1bbdebddfed2)

We will just need four of them for this step:

![image30_008](https://github.com/user-attachments/assets/7ff23dc5-f4ea-4ab6-b718-f1a2a4c73101)

Note that each roller has two ends. One end has a shallow recess to hold the magnet. That is the side we want to use.

![image11_002](https://github.com/user-attachments/assets/db4225ec-9c28-4e9d-9354-3f40dbf71704)

Again using a drop of glue place a magnet in the end of each of the four rollers. Note that the magnet will be slightly recessed below the top of the roller. 


Be careful, the magnets will try to stick to things while the glue is drying.


The magnet is symmetrical so it can be placed in either orientation.

![image22_008](https://github.com/user-attachments/assets/7f556c87-a8a9-4ba8-a1bc-e591af4480ae)

Now set those aside to dry. Be careful not to put them too close together while drying because the magnets will attract and stick together. You have to place them further apart than you might expect. 

![image34](https://github.com/user-attachments/assets/92bcbf31-5f72-4875-a685-2994f2929a50)


Next, we can start working on assembling the rest of the arm. To do that we’re going to need to collect our hardware bags, our thread locker, a DC drive motor, and an encoder board.

![image25_008](https://github.com/user-attachments/assets/36d35182-3050-48e2-a4a7-5d7f667fe7cc)


The first thing that we are going to need to do there is to insert these two nuts into the end of the arm because they will be covered later when we need them. Don’t forget this step or you will have to backtrack a lot later (a mistake I’ve made many times). Using the allen wrench as a guide can help get them in place.

![image32_008](https://github.com/user-attachments/assets/5450f9e8-b6b9-4e96-adc7-935cd6bb8655)


Next place the encoder board over them. There are two guide pegs which will hold it in just the right place.

![image29_008](https://github.com/user-attachments/assets/07d9af97-32b1-41ae-b0f6-e3217dadbaa8)


Then place the motor over that. The orientation of the motor does not matter. 

![image1_006](https://github.com/user-attachments/assets/df04ab4b-481e-4cd4-b018-a0a5239242dc)


The bolts which connect the motor to the arm can wiggle themselves loose over time so we’ve included removable thread locker in your kit. Place a small amount of thread locker on four bolts.


You will need to cut the end of the thread locker with scissors or a knife. Note - Do not use the super glue here. The thread locker is a blue liquid, while the super glue is a clear gel.

![image20_003](https://github.com/user-attachments/assets/e41514a9-b011-48ff-b6fd-c252545b4650)

And then bolt the drive motor into place.

Community Note: There is some play in how these bolts attach the motor and having the motor further forward (to the right in the picture) is better than further back. If you find that the gears are too tight in later steps you can loosen them up by moving the motor forwards.

![image36_006](https://github.com/user-attachments/assets/a9f76ec2-2c90-4aef-a45d-5cffbb29c9fc)

Next we need to collect our drive gear (which has a D shaped opening in the center) and set screw:

![image3_008](https://github.com/user-attachments/assets/de169ea9-d933-4429-8ff2-ac5994c67363)

Thread the set screw part way into the drive gear:

![image21_007](https://github.com/user-attachments/assets/4efa15ee-9c6a-4859-86ea-c4577924e97c)

Then attach it to the motor shaft. The gear should be almost but not quite touching the plastic or lifted up about the width of an index card or three pieces of paper. Tighten the set screw to lock it in place.

![image4_008](https://github.com/user-attachments/assets/5b4c0c45-0dd4-480a-b6f0-f18a671250c1)


Next collect the idler gear (which has a round opening in the center) and the idler shaft along with our packet of silicone grease.

![image13_008](https://github.com/user-attachments/assets/b6aa0f07-1b35-4b94-a906-613e7e82332a)


Insert the idler shaft into the arm next to the drive gear. Feel free to bang it on the table a bit to make sure it’s all the way in.

![image16_008](https://github.com/user-attachments/assets/43596e04-ccfc-45ab-b3de-18555b6396ae)


Then place a small amount of silicone grease on the shaft and inside the gear and then slide the gear into place.

![IMG_3371_008](https://github.com/user-attachments/assets/f3721b55-7120-44c2-928c-a7be01db104e)

Next we need to press bearings into the arm. Press two bearings into the opening in the front of the arm. You can use one of the rollers that doesn’t have a magnet to help press them into place if needed.

![image8_002](https://github.com/user-attachments/assets/bd14c0df-45ee-4787-a86b-e7e455dcb9a4)

Next set this half of the arm aside and we will work on the other half. First we will need to press three bearings into a new arm half. The middle bearing on this side is a bit snug so some GENTLE taps from a hammer can help press it into place.

![image19_008](https://github.com/user-attachments/assets/c4c5c7ca-e2bc-4b33-87de-4df90e85fcd9)

Next we will attach the belt guard. Insert three nuts into the holes in the top of the belt guard with the open end of the nuts down.

![image35_008](https://github.com/user-attachments/assets/2e0c5d35-25a9-48db-84e0-fa011696c396)

Then add three bolts to hold it in place.


Next we need to wait for the glue to finish drying on our other parts before we can proceed. Feel free to repeat the steps up to this point for all four arms. If you feel like continuing to work on other parts of the machine while the glue is drying you can start on the Assembling the Sled section.


This next part where everything comes together sort of happens all at once and can be a little tricky. Don’t feel frustrated if it takes a couple of tries to get it right. With practice it can be pretty smooth, but the only way to get the hang of it is to try.

![image33](https://github.com/user-attachments/assets/88491a9c-bcb9-41d3-805a-84a7934d6fa5)

First place the spool with the belt wrapped around it onto the top of the arm so that it engages with the drive gears. The fit will be quite snug.

![205ae4980e697c0f9270b9e9c1200cdb06d8bfa9_2_750x1000_008](https://github.com/user-attachments/assets/d25be824-bd68-43ed-91e0-8b17364cb0c3)

![5430d717135c8b2eaee948a50da232e88ccb4d9e_2_750x1000_007](https://github.com/user-attachments/assets/3b186ca9-2680-48b9-be1f-ec7309ef2ac4)


Note that when we route the belt we want it to pass in front of the drive gears, not behind them.

![image9](https://github.com/user-attachments/assets/4203930e-626b-497c-b582-ee6a4b1d2d37)

![Screenshot+2024-03-10+at+2 03 19 PM](https://github.com/user-attachments/assets/8f902a7b-f60b-4bd0-9e03-2c83cf27693d)

Next on the other side of the arm thread the end of the belt through the belt guard and insert the two rollers. **Note that one roller has a magnet and the other does not. The placement of the magnet is very important. It must line up with the sensor (in green on the right) on the other half of the arm when the two halves come together.**

Then press the two halves together. This is much easier said than done and this is the trickiest step of the build process. 


Generally I start from the back of the arm (the side that doesn’t have the belt coming out of it) and work my way forward. 

![image7_007](https://github.com/user-attachments/assets/af059d15-24b9-48e2-b0d4-7b14fbe1aa25)

There are three things which will need to go into place. First the idler shaft needs to align with the hole on the opposite side. Then the drive shaft needs to align with the opening in the bearing on the bottom, then the rollers will need to line up with bearings on the other half. 


I find that by checking each one of these locations in turn and getting them lined up the two halves will come together. It can be helpful to use the allen wrench to jostle the rollers around to make them line up.

![image28_004](https://github.com/user-attachments/assets/0e642511-5bc5-49ad-a69c-85debaf74b65)

Then we join the two halves together with eight bolts. 


Two of the bolts at the front already connect to the captive nuts there. If those nuts are out of place, I find the Allen wrench works well to jostle them back into the right position.


The other six go around the center of the spool. The direction of these bolts can go in either direction. Don’t over tighten these, they just need to be tight enough to keep the two halves together.

![image5_006](https://github.com/user-attachments/assets/a44da471-140f-4bc0-8af5-58b698555f43)

Finally we need to attach the belt end to the end of our arms. Gather the eight belt end parts:

![image24_007](https://github.com/user-attachments/assets/c0497cc5-72fa-40f6-bd04-1a1ab6bd58f5)

Then take two of the belt ends and the end of one of the belts extending from our arms:

![image15_008](https://github.com/user-attachments/assets/58a83a5b-846f-49a1-82eb-afb90b097b80)


Fold the belt over and press it into the slot in the belt end so that the end protrudes only slightly. This can be tricky to do because the fit is tight (it needs to be tight to handle the force). I find that starting with the loop in the belt at the open end of the slot and pushing it forward works well. You can use the allen wrench to lever it into place.

![image27_004](https://github.com/user-attachments/assets/1657b930-988f-4280-8c75-a7f30093edc6)

Then press on the other half of the belt end. 

![image18_005](https://github.com/user-attachments/assets/6389fb68-ac77-4bb5-9f98-d1b66dd4aec8)

And secure it with a bolt and non-lock nut.
Congratulations! You are done with the most difficult and longest part of the assembly process!


## Assembling The Sled



Find any of these steps confusing or get stuck? Don’t forget, you aren’t alone! Maslow is a community driven open source project. Ask in the forums and we’ll figure it out together!

Next we are going to assemble the Maslow4 sled. To do that we will need our sled, our stepper motors, our linear rods, our linear rod clamps, and our dust cover.

Amongst your hardware are a few bolts which are shorter than the rest. Find eight of these.

Using those eight smaller bolts attach the stepper motors to the sled by bolting through the sled and into the four threaded holes in each stepper motor. Orient the cords as shown in the next step.

Orient the stepper motors so that the cord is facing towards the edge of the sled.

Using six of the regular bolts and nuts attach the dust cover. I find it easier to flip the sled upside down for this step and drop the nuts down from the top and insert the bolt from the bottom.

Find your two linear rod clamps. 

Because of our new and improved packing system these will likely be installed on the sled and you will need to take them off.

Insert linear rods into the two openings for them on either side of the sled. A GENTLE tap with a hammer can help make sure that they are fully seated.

Press the linear rod clamps into place around each rod.

Bolt them in place using eight bolts. Be careful to tighten these evenly going around in a circle and tightening each one gently before moving on to the next. Do not overtighten these. They do not need to be excessively tight and you can break the sled by tightening these too hard. 

That’s it! You’ve built your Maslow4’s sled!
Congratulations! Your sled is complete.











    



# Building 4 or 4.1 tips
- Use a straw to push the nut up in the hexagonal hole while getting started with a bolt.
- add some more!!

# Details on Optional addtions (add details and instructions) 
  1. 3D printed **button pushing insert** to make it much easier to insert and remove router bits
2. **Dust control** vacuum system. Starting with a 3D printed nozzle adapter for the machine and then a hose (antistatic is good) and dust collection vacuum system.
3. **More CNC router bits** 1/8 in straight flute, Pointed engraving tip, rounded hollowing bit
4. **Collet adapter** for 1/8 in shaft router bits
5. **Cord**, wire, and vacuum hose **control system** so that the connections don't pull on the machine too much.
6. **A way to hold the material in place while cutting**. Rug no slip fabric or double sided tape or brass screws or wood chocks or a vacuum system or....   Depends a lot on your material and setup. Don't use steel screws anywhere near your cutting area or the blades will colide with them.
7. **Metric meausring tape**
8. **Emergency Stop Button**  Extension cord with a clearly marked button for turning off power to the router and robot. 
9. **Handle** for moving the machine (?Best practice?



    # Routers lists and library
   
    # Router bits lists and library
   
    # CNC, CAD and CAM software lists and library
   
    # Material to cut lists and library
   
    # Frames and frame library
   
    # Connecting to the machine
   

# Updating the firmware
  The ESP computer that the Maslow is built around is a flexbile computer used in many smart devices and robotics projects.  Maslow's interface is built off of a generic ESP software called 3DFluidNC which was designed for controlling CNC machines. Maslow's software and user interface sits on top of and builds off of this base. 
 
 
    
    
# The Control panel and software

# Other things you can control in the software.

# Moving the robot around by jogging

# G-code

# Feed rates library

# Test Cut designs and grids

# Making a 2D design as a vector drawing .svg

# Changing a 2D design into G-code .nc for cutting

# Uploading G-code .nc to the machine

# Fixing the work to the wasteboard

# Inserting a cutting bit

# Inserting a drawing pencil or pen (no idea how to do this)

# Drawing with the machine

# Preflight check

# Cutting a 2D design

# Designing a layered 2D design with mulitple depths

# Preparing a layered 2D design as G-code

# Cutting a layered 2D design with multiple depths

# Switching cutting heads between operations

# Designing a 3D sculpted surface design

# Preparing a 3D sculpted surface design as g-code

# Cutting a 3D sculpted surface design

# Designing a 3D piece of furniture from multiple 2D panels

# Abundance software

# Paramaterized design for 3D

# G-code for furniture

# Cutting furniture.

# 3D design as stacked layers

# Cutting 3D as stacked layers

# G-code for stacked layers of 3D designs

# Other design methods

# Including bushings and hardware in designs

# Design library (perhaps one for parts of desgins and one for full projects?)

# Accuracy and precision

# TROUBLESHOOTINg

# Fun stuff

