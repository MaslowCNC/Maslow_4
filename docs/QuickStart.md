# Quick Start Guide

This guide is intended to get your Maslow up and running in as few words as possible. It is intended to be used immediately after completing assembly so we can assume that the user has already assembled their machine and they are now looking for how to set it up.

  
## Choosing your Anchors

Maslow 4 is designed to turn any flat rigid surface into a large format CNC router. This is powerful, but it can lead to an overwhelming number of options. 

The best option is to attach the machine directly to a concrete floor. This can be done by either attaching 3D printed anchors to the floor or by adding threaded inserts into the concrete.

<img src="https://github.com/user-attachments/assets/d23d8f62-3247-4021-b801-6f804004a208" alt="Floor anchor" width="400">

The anchors are sized to fit a 10mm or 4/8ths inch bolt.
 
<img src="https://github.com/user-attachments/assets/eb3b7083-ba87-4b3d-877e-17002e466056" alt="Threaded insert" width="400">
 
You can find a complete list of different anchor types here: [ADD LINK]

  If you don't have a floor to connect to you can construct a flat rigid surface to use the machine on. You can find instructions to assemble a basic one that we recommend here: https://www.maslowcnc.com/frame-options

<img width="500" alt="image" src="https://github.com/user-attachments/assets/8e36c830-4d37-4d99-a702-d1638e9ebba6" />

We've put together a tool to help you to better understand what size of frame you might want and how that will impact your cutting area. You can find that tool here: https://maslowcnc.github.io/Layout-Simulator/

<img src="quick-start/images/Layout%20Simulator.png" alt="Layout Simulator" width="600">

- Size and other considerations
  - Belt ends need to be loose to rotate in the XY directions but gently held in the z direction so they don't pop up and down.
  - Belt ends will be taken on and off a lot. 
  - Can be horizontal to 20 degrees from vertical.
  - Does not have to be exactly rectangular.
  - Software limits basic frames to less than 5 meters wide and tall.
  - Belts are 4.4 meters long. Cutting area can not be farther than that from an anchor. 

## Connecting
	
  Maslow4 is controlled using a built-in interface accessible from your web browser. You can connect to Maslow4 from any Windows, Mac, or Linux computer or iOS or Android tablet or phone. You do not need to install any software. 

Maslow4 will create a wifi network called **“maslow”** which you can connect to. The default password for this network will be **“12345678”**.

Connecting to the network will automatically open the user interface on most devices. If it does not you can type **192.168.0.1** into your web browser to open the interface. 

<img src="user-guide/images/guide-12.png" alt="File Upload" width="600">

- Connecting Maslow to your wifi

  While you don't need to connect Maslow to your home wifi network for it to work, if you have wifi available the next thing to do is to connect to it.

  To connect Maslow to your wifi press on "Setup" in the top right corner.

<img src="quick-start/images/Screenshot%202025-12-15%20at%203.11.49%E2%80%AFPM.png" alt="Press setup" width="600">

  Then press "Config"

<img src="quick-start/images/Screenshot%202025-12-15%20at%203.12.30%E2%80%AFPM.png" alt="Press config" width="600">

  Then enter your wifi network information and press save.

<img src="quick-start/images/Screenshot%202025-12-15%20at%203.13.02%E2%80%AFPM.png" alt="Enter wifi name" width="600">

  Maslow will try to connect to this wifi network every time it powers up. If it can't find that network or cant connect for some reason it will create the Maslow wifi network for you to connect to it. Turn your Maslow off and back on to let it connect to your wifi network.

  Once Maslow is connected to your wifi network you you can access it by navigating to the address **maslow.local** from any browser. If you are having trouble finding it try a different device or browser.

  As a last resort you can always find your machine's IP address by counting the blinks of the blue light.

  <img src="user-guide/images/guide-08.gif" alt="LED blinking" width="400">

## Updating the firmware

Maslow4’s firmware is improving regularly.

Luckily updating the Maslow4 firmware is easy. 

To update Maslow4’s firmware click on the FluidNC tab at the top of the screen, then click on the Update the Firmware button, and select your new firmware file.

You can always find the latest firmware version at [https://github.com/BarbourSmith/FluidNC/releases](https://github.com/BarbourSmith/FluidNC/releases)

There will be 3 files that you need to download, **firmware.bin**, **index.html.gz**, and **maslow.yaml**. When you download the files, make sure your computer does not change their name. You must change the name back if this happens.

Note: When you first connect to Maslow it will create a popup to control the machine. On some devices you cannot upload files from within that popup (the window won’t open). The solution is to connect to Maslow from a regular browser window.

Note that to update from a firmware version before 1.0 to a version after 1.0 you will need to use a USB cable. There is a video walkthrough for that process [here](https://youtu.be/od7DpdLel6A?si=xv1Zp3AIZFgRoeZ_).

<img src="user-guide/images/guide-32.png" alt="firmware update" width="500">

There are two other files which you will need to update periodically. These can be found by clicking on the FluidNC tab and then clicking on the files button.

<img src="user-guide/images/guide-14.png" alt="these files" width="500">



This will show you your system files.

To upload a new file click the Upload files button at the top of the screen. If a file with the same name already exists it will be replaced.

index.html.gz controls how the machine interface looks. If you wanted for example a dark mode, replacing this file would give the interface a new look. I expect that there will be a number of community created UI options created quite quickly.

maslow.yaml contains the configuration settings for your machine. Your calibration values are stored here. You may not need to update the yaml each time you update the index and the firmware.

<img src="user-guide/images/guide-25.png" alt="maslow yaml" width="500">





## Extending and Retracting the Belts

The Maslow 4 belts can be retracted for storage and extended for use. 

Every time that the belts are retracted the machine will use that as an oportunity to reset it's understanding of how long each belt is. This is done by monitoring the current required to retract each belt. If your machine is in an unknown state retracting the belts will help it to understand exactly where it is.

To retract the belts press **Setup -> Retract All**

<img src="quick-start/images/Retract%20All.png" alt="Retract All" width="500">

If all of your belts don't fully retract you may need to increase the amount of force that the machine uses to retract. You can do this by clicking on **Config** and increasing the retraction force. The lower that this number can be the better.

<img src="quick-start/images/Retraction%20Force.png" alt="Retraction force Image" width="500">

When you are ready to extend your belts, press **Extend All**. Extending the belts can take a little practice. To prevent tangles the belts will only extend as long as you are pulling on them. Use a rocking motion to start the belts extending and then pull steadily.

<img src="user-guide/images/guide-26.gif" alt="guide-26.gif" width="400">

The belts will extend to the length set in the config file. If you need to extend more belt to reach your anchor points, adjust the number there and press **Extend All** again.

<img src="quick-start/images/Extend%20Dist.png" alt="Extend Dist Pic" width="500">

Once all four belts are fully extended you will hear the cooling fan turn off. Connect each of the four belts to your four anchor points.

<img src="user-guide/images/guide-35.jpg" alt="guide-35" width="500">

## Finding your anchor point locations

If you haven't prevously connected your machine to these anchor points, you will need to locate them. This can be done with a tape measure, but that is slow and error prone. 

Press **Find Anchor Locations** to have the machine take a series of measruements to automatically locate the anchor points for you. The machine will move through a grid of points and take measurements at each one. 

Be sure to leave your web bowser tab open through the entire process because the calculations will be done there since your computer has much more processing power than the ESP32 in the Maslow. 

Keep an eye on the machine during this entire process, it may be tempting to walk away, but it is important to keep an eye on it.

<img src="user-guide/images/guide-02.jpg" alt="guide-02 Image" width="500">

## Generating gcode 

## Uploading to Maslow

Once you have  gcode that you want to use to cut something, you will use the "upload" button on the right side of the maslow web interface. That will trigger a dialog box for selecting a file on whatever computer/tablet you're using. Navigate to the gcode file you want to send to the Maslow, and select it. (There are other ways of doing this, but this is the easiest.) Depending on how your home network is set up, the computer you use to design or download a project and generate g-code may not be the one you use in your shop to control the Maslow, but that's OK. The Maslow's built-in web server can deliver multiple windows. (The contents of those windows won't be synchronized, but file you upload will be visible from whatever computer you're using, as long as it's got a browser window open to the Maslow.

## Move the machine around

This is the Maslow's motion panel:
<img width="445" height="245" alt="Screenshot 2026-01-02 at 17 53 26" src="https://github.com/user-attachments/assets/b971acf9-b550-45e9-a871-738b960de4db" />

If the belts are already tensioned, you can make the Maslow move by clicking on the arrows in the motion panel. The number in the center is the distance that the Maslow will move with each click, and can be edited by clicking on it. Make sure there's nothing on the work surface that could get in the way before you move the machine. (The first time you move the machine around, you will probably want to label the work surface with arrows corresponding to the directions on the motion panel so you can orient material to be cut.)

## Define home position

# X/Y
Each time you use the Maslow, you can redefine the X-Y home position. This will come in handy if you want to cut the same G-code several times in different places on a sheet, or if the g-code file you are cutting has its X-Y zero position somewhere inconvenient.  The position display (upper left) has a little pink dot representing the current location where the Maslow will cut, so you just move the machine to where you want the home position and then set it with a long press of the button in the motion panel.

(The position display can toggle between 4 different modes, so if you don't see the pink dot on the closeup display, click to one of the others that can show the entire work surface. One mode shows color-coded cut accuracy, so you may want to adjust where you plan to cut to get the best results.)

# Z
Any time you install a different router bit, you will need to redefine the Z home. Use the Z-up and Z-down buttons to bring the bit to the right height, typically just barely above the surface of the material you are cutting. The number between the Z-up and Z-down buttons is the distance the bit will move each time you press a button, and you will likely want to edit it to a smaller number when making adjustments. You will probably also want a stiff piece of thin plastic about 30 cm long to push under the machine so that you can tell when the bit is near the surface. Once you have the bit where you want it, press the "Define Z Home" button.

Because there's tension on the belts, and the belt angle changes when you move the router up or down, the Maslow will appear to jump when you press the Z-up and Z-down buttons. Don't worry too much. 

## Running a file

Before running a file, you need your workpiece in place. Release tension on the belts, slide the piece to be cut under the Maslow and secure it to the worksorface, then apply tension again.

Select the file to run from the dropdown menu just to the left of the "Upload GCode" button. A preview of the cutting paths will appear in the position display. If you need to, set the XY home position.

# Preflight
Take a close look at the cutting preview to make sure the Maslow is planning to cut the shapes(s) you want in the position(s) you want. 
Make sure the workspace is clear of anything that might foul the machine or the belts.
Make sure that the router spindle is free to turn.
If you have dust collection (and you should) turn it on.

Turn on the router.
Press the green button on the Maslow web interface. (Be ready to hit the Stop or Alarm button if anything goes wrong.)

Keep watching until the cuts have completed. 

When you're done, turn off the router, then the dust collection, then (if you're done with that workpiece) release tension on the belts.

