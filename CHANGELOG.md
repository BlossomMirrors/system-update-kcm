# 0.4.0

- Run upgrades in a D-Bus activated helper so downloading and staging continue in the background with a working Plasma job notification and cancel button after System Settings is closed
- Reattach to a running upgrade, including its transaction log, when the module is reopened
- Show a remaining time estimate and an installing phase in the progress view
- Offer a restart instead of installing again once an update is staged
- Fix having more than one bootc image always display an update
- Add support for p2pupdated
