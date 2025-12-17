# Maslow Readme for machines version 4 and 4.1 Current through 2025
## Quicklinks:
Website where you can buy a kit and current assembly instrucitons:
<https://www.maslowcnc.com/>

📚 **Documentation Site (GitHub Pages):**
<https://maslowcnc.github.io/Maslow_4/>

📋 **Changelog (What's New):**
[CHANGELOG.md](./CHANGELOG.md) | [Release Notes v1.16](./docs/ReleaseNotes_v1.16.md) | [Release Notes v1.15](./docs/ReleaseNotes_v1.15.md)

Assembly instructions in wiki:
<https://github.com/MaslowCNC/Maslow_4/tree/assembling-the-arms-4-1>

Repository and wiki:
<https://github.com/MaslowCNC/Maslow_4/wiki>

Forums:
<https://forums.maslowcnc.com/>

How to edit this wiki:
<https://github.com/MaslowCNC/Maslow_4/blob/Maslow-Main/docs/HowToEditThisWiki.md>


This is a rough draft. For more info go to <https://www.maslowcnc.com>

![Maslowrender](https://github.com/user-attachments/assets/305da71c-ce76-40d9-ad8a-40902add06af)



![LogoWithR](assets/README-assets/LogoWithR.webp)

# INTRO


<img width="2500" height="1875" alt="image" src="assets/README-assets/image_ed9fcca8.png" />


## What is it? 
Maslow is a DIY 3 axis (x,y,z) computer numerical control (CNC) robot that controls a router to cut wood, plastic or other flat material.  It has been designed to work well on a 4x8 foot sheet of material or smaller and it is able to sculpt and cut vertically about 60 mm or 2.5 inches deep.  Maslow is designed around a small sled that carries the router and is anchored to an external frame by long belts that it uses to pull itself around.  This means that the core robot is very portable. It does require a stiff frame or anchors external to the robot.  It rides on top of the work being cut so it works well on cutting and sculpting shallow shapes but not giant bathtub sized hollows. Maslow is focused on accessiblility. 

Maslow's control software is a local website hosted inside the machine that you can access through USBC cable, Wifi direct to the machine, or through your local wifi network. It can be used with a phone or a computer. It recieves standard Gcode machine instruction files (.nc)  You will need cnc design software that can generate Gcode. There are many free or fancy options. 

The control software is built on top of FluidNC an open source cnc control software for ESP32 computers. Maslow4's pcb is an esp32 computer.


The FluidNC project wiki is here: 

<http://wiki.fluidnc.com/>


Github:

<https://github.com/bdring/FluidNC>


Donate to FluidNC:

[![](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=8DYLB6ZYYDG7Y)


Maslow4 is different from a gantry designed CNC router which are designed around a table that fits the material inside and then the robot moves forward and back on rails. It is much much less expensive than a gantry machine. It is in general less accurate than a good gantry machine. Gantry machines can also be 4 or 5 axis machines in which they can rotate the material in order to cut it at different angles. 


Maslow is a project developed by Barbour Smith and a community of volunteers and forum members as an open source project that is still under development.  It is portable, exciting, inexpensive and perhaps frustrating and still requires some troubleshooting and figuring out to get started.  Barbour has been working on iterations of this for more than a decade and many people have been successful at making many exciting projects including saunas, tables, signs, and a 30 foot catamaran! (link to project gallery) but it is not at the moment a perfect plug and play tool.  **If making your own portable blade wielding robot in an enthusiastic online community sounds fun, then you are probably in the right place.** 

<img width="2500" height="1667" alt="image" src="assets/README-assets/image_d57a103a.png" />

# History (I don't know a lot here) 
 In ???? Barbour Smith designed the first Maslow as a wall mounted CNC routing robot that hung from two chains and pulled itself back and forth across a space. The robot at that point was designed around an Arduino Mega 2560 microcontroller and was an open source design. Barbour set up the forums and github groups and sold ??? machines. 
 
 
 It is still possible to make a Maslow Version 1, 2, or 3 and many people have been happy with them as useful tools. Makermade was a company that sold version ?2? under the open source license that is not affiliated directly with Maslow's developers. 
 
 
 The current version Maslow 4 was prototyped as 3D printed parts with standard hardware. It has four machine belts instead of two chains. The parts are not interchangable with earlier versions. It uses a custom printed circuit board PCB built around an ESP microcontroller. The board also incorporates motor controllers. 
 
 
It is still possible to 3D print your own parts and replacement parts. It is still possible to design and program your own generic ESP microcontroller and use off the shelf motor controllers. In ?2023? Barbour ran a successful Kickstarter campaign with which he used the proceeds to design and have injection molds made for injection molded parts and better compact custom PCBs. Injection molded parts are much stronger than most 3d printed parts. This is what you are buying when you buy a Maslow kit. The custom PCB, the custom wires, the motors and custom made hardware, nuts and bolts, and the injection molded plastics with fiberglass inclusions. 4.1 was the result of a second kickstarter campaign that upgraded the PCB, wire connectors, the nuts and bolts and other metal hardware as well as a better spool design.


# Community members who have made significant contributions:
bar founder and primary developer

dlang ?Programming?

Ian_ab?Programming? 

# How to get involved
Maslow is a truly open source project. The forums are active with helpers and anybody can add to the discussions there or add to the project here in the github repo.  You can't mess things up here as final commits have to be checked.
The creative community needs to share fun projects, fun adapatations of the robot and support frames, hacks and troubleshooting tips, programming fixes, and documentation. 


/Firmware is edited and developed by volunteers, you can try too, the Github Maslow AI can be asked to change the programming. 


Three D Printing files and parts lists will soon be in the repo for downloading and editing or adapting


/docs holds documentation and instructions.  If you have a good idea to make things more clear or help other people understand the machine you could add something there


To edit a file here navigate to the file you want to edit, open it, then click on the little pencil in the top corner and it will open an editing window. When you are editing you are making a temporary branch of the main repo. This branch is a copy of the main repo that can be compared and if useful written back into the main repo. The human readable document pages are written in markdown language, a kind of simple word processor. Markdown instructions are in a link at the bottom of the editing window.  Once you have edited the document you can commit changes and then make a pull request asking for it to be written back to the main branch.  


# Future of the project
Currently the focus is on making the Maslow4 run quickly and easily mostly through firmware development. Community members are playing with different 3d printed adaptations of the main parts as well as both larger and smaller frames. The goal would be to keep developing the software now and then have an updated hardware kit in several years. A sister project to Maslow is Abundance, which is working towards being an open source web browser based parametric cad system that could feed directly to Maslow <https://abundance.maslowcnc.com/>
(written 2025) 

# Safety and hazards
Power tools are inherently powerful and can be dangerous.  This is an open source project created and maintained by amatuer enthusiasts with designs that have not been tested or evaluated for safety. Follow any instructions here at your own risk and using your own judgement.  The authors of this project make no claims about the saftey or suitability of the designs, ideas, or instructions. Any safety advice is provided in good faith. Use and modify the information in this project at your own risk. 

Below is a list of common sense hazards for you to consider

## Hazards may include:
- **Electrical shock or fire** if used in a damp or wet area or if electrical insulation or protective covering is damaged or removed. Always check your wires, connections and PCB cover for damage. Make sure airflow to the machine is not blocked or restricted. Different routers and spindles will require different airflow. Do not leave the machine unattended while it is in use. Have a plan to cut power to the machine in an emergency. 
- **Dust inhalation and dust fire**. Routers create dust and heat as blades cut through material.  Evaluate the material you are cutting and take dust mitigation measures including working in the open air, using dust vacuum systems, dust masks and appropriate respirators.
- **Friction fire hazard.** Blades create heat through friction, this can cause material being cut or other nearby items to ignite, smoke, or smolder. This is especialy dangerous when a power tool gets stuck in one place or bound up in the material being cut. While the machine is running do not leave it unattended and test and evaluate the material being cut and the feed rate, moving speed, and rotational speed for suitablility for each job. Materials may produce fumes while being cut. 
- **Laceration, burns, pinching, and crushing**. Never touch the blade of a router without saftey measures. Router bits are very sharp and will be hot after use. The motors and belts can entangle hair or loose clothing and fingers or other body parts could be pinched or torn while the machine is moving. Care has been taken in the design to minimize these risks.  Keep clear of the the machine while motors are engaged. Make sure both the router and the control board are unplugged and off before changing bits or touching the machine.
- **Eye, skin, or equipment damage from backlash or flying debris**.  Spinning power tool blades can throw pieces of themselves or cut material at great speed. Robotic motors put belts and parts under strain which can result in breakage and pieces being thrown with speed. Wear eye protection at all times when the machine is running and only use the machine in a controlled environment where bystanders are protected from flying debris. Do not leave the machine unattended.
- **Hearing Damage**. Routers and robots can easily exceed safe noise thresholds while running. Wear appropriate hearing proection while the machine is in use.


## Overall saftey meaures
- Regularly check your machine  for evidence of wear or damage.
- Only use in a safe and controlled space. 
- Do not leave the machine running unattended.
- Use personal protective equipment including masks, respirators, gloves, hearing protection,  and eye protection. 
- Do not touch the machine while it is moving or turned on.
- Do not change blades while the machine is plugged in. 
- Have a fire suppression plan and equipment.
- Consider how you will cut power to the machine in an emergency.
- Check for loose parts, tools, keys, and if the blades are in an appropriate place before providing power to the machine. 
- Train yourself and anyone who will come in contact with the machine on your saftely plan. 

CNC routers are nice in that they may be operated at a greater distance than a handheld router.  Many of the vibration, inhalation, and flying debris dangers are less with a robotic controler.  CNC machines are dangerous when people leave them unattended and beacause there are more motors, more electrical connections, and more moving parts. 
