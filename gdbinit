# the jlink debugger seems to want a pause after reset before we tell it to start running
define restart
  monitor reset 
  shell sleep 1
  cont 
end
