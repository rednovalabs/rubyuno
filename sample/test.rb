require './lib/rubyuno'

ctx = Uno::Connector.bootstrap
smgr = ctx.getServiceManager
desktop = smgr.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
doc = desktop.loadComponentFromURL("private:factory/swriter", "_blank", 0, [])
doc.getText.setString("Hello from Ruby!")
