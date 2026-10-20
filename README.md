# Open MiniFuse 1 Controller
Control your MiniFuse 1 directly from command line.

You can turn on, off, or toggle the Instrument mode (inst), phantom power (48v), and direct monitoring (dm).

You will obviously need the driver installed, but otherwise this interfaces directly with the driver with no dependencies.
It's hard coded for the MiniFuse 1 since that is what I have.
I am certain it can be updated to work with 2/4 but I don't have those interfaces to test with.

Use at your own risk! It's very possible this only works on my computer, if it works for you, great, if not then oh well, if it breaks your hardware then refer to the previous statement.

This is here for historical purposes and I don't expect to ever update it.


usage:
```
minifuse_ctl status
minifuse_ctl <inst|48v|dm> <on|off|toggle>
```


# Rant
Having changed to the MiniFuse 1 from one with a physical toggle switch for phantom power, it was a surprise that I couldn't automatically toggle it on at startup.
Reaching out to Arturia was pointless "bUt YoU dOn'T wAnT tHaT, YoU wIlL dAmAgE yOuR mIcRoPhoNe!11"

All my microphones require phantom power, I need it turned on every single time. I am not stupid.
