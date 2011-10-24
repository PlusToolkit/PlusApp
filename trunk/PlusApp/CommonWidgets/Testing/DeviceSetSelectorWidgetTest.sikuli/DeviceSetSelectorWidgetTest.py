import sys

#------------------------------------------------------------------------------

# Assembles the application executable command properly according to the script's arguments
def getCommandFromArguments(plusDir=''):
  if len(sys.argv) > 1:
    exe = sys.argv[1]
    
    # Concatenate the command line arguments to the executable name
    for argnum in range(2,len(sys.argv)):
      if argnum == 2:
        exe = exe + ' ' + sys.argv[argnum]
      else:
        exe = exe + ' "' + sys.argv[argnum] + '"'
  else:
    # Temporary setting of the executable (for trying in Sikuli IDE)
    if plusDir == '':
      exe = ''

    exe = '"' + plusDir + '/PlusApp-bin/DevEnv_Release.bat" "--launch" "' + plusDir + '/PlusApp-bin/bin/Release/SegmentationParameterDialogTest" "--device-set-configuration-directory-path=' + plusDir + '/PlusLib/data/ConfigFiles" "--image-directory-path=' + plusDir + '/PlusLib/data/TestImages"'
  
  return exe

#------------------------------------------------------------------------------

# Define function that is run when error occurs
def captureScreenAndExit():
  message = "The screen capture of the application in time of the error was saved to: "
  message += capture(SCREEN)
  print message
  closeApp(appTitle)
  exit
  
#------------------------------------------------------------------------------
# Main program

exe = getCommandFromArguments("D:/devel/Plus-build-bin")

appTitle = "SegmentationParameterDialogTest"

testApp = App.open(exe)
wait(10) # Wait for the application to initialize (else the next wait check does not not run properly)

try:
  connectButton = wait("ConnectButton.png", 60)
except FindFailed:
  print "[ERROR] Application did not start!"
  captureScreenAndExit()

# Get the region of the segmentation parameter dialog window
applicationTopLeft = connectButton.getTopLeft()
applicationTopLeft = applicationTopLeft.left(205).above(295)
windowRegion = Region(applicationTopLeft.x, applicationTopLeft.y, 495, 355)
comboBoxRegion = Region(applicationTopLeft.x, applicationTopLeft.y, 495, 600)

# Select a configuration that cannot connect
try:
  comboBoxDropDown = windowRegion.find("DropDownArrow.png")
except FindFailed:
  print "[ERROR] Cannot find device set combobox!"
  captureScreenAndExit()

click(comboBoxDropDown)

try:
  sonixTouchWithAscensionItem = comboBoxRegion.find("DeviceSetItemToFail.png")
except FindFailed:
  print "[ERROR] Cannot find SonixTouch Ascension3DG item!"
  captureScreenAndExit()

click(sonixTouchWithAscensionItem)
click(connectButton)

# Verify failed connection
try:
  windowRegion.wait("Connection failed", 10)
except FindFailed:
  print "[ERROR] Cannot find connection failed warning text!"
  captureScreenAndExit()

# Connect to device set that is supposed to work
click(comboBoxDropDown)

try:
  segmentationParameterDialogTestItem = comboBoxRegion.find("DeviceSetItemToConnect.png")
except FindFailed:
  print "[ERROR] Cannot find SegmentationParameterDialogTest item!"
  captureScreenAndExit()

click(segmentationParameterDialogTestItem)
click(connectButton)

try:
  freezeButton = wait("FreezeButton.png", 10)
except FindFailed:
  print "[ERROR] Connection failed!"
  captureScreenAndExit()

closeApp(appTitle) # close the window - stop the process
