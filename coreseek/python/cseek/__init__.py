# demo sphinx config by python.
'''
source xml
{
    type                    = xmlpipe2
    xmlpipe_command = cat var/test/test.xml
}

index xml
{
    source            = xml            
    path            = var/data/xml 
    docinfo            = extern
    charset_type        = utf-8
}

'''

class DefaultSqliteConf(object):
  def __init__(self):
    print "hello , I'm here."

  def loadConfig(self, wconf):
    print dir(wconf)
    if True:
      wconf.addSection('source','xml')
      wconf.addKey('type','xmlpipe2')
      wconf.addKey('xmlpipe_command','cat var/test.xml')
    if True:
      wconf.addSection('index','xml')
      wconf.addKey('source','xml')
      wconf.addKey('path','var/data/xml')
      wconf.addKey('docinfo','extern')
      wconf.addKey('charset_type','utf-8')



