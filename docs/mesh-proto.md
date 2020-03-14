TODO:
* reread the radiohead mesh implementation
* read about general mesh flooding solutions
* reread the disaster radio protocol docs

good description of batman protocol: https://www.open-mesh.org/projects/open-mesh/wiki/BATMANConcept

interesting paper on lora mesh: https://portal.research.lu.se/portal/files/45735775/paper.pdf
It seems like  DSR might be the algorithm used by RadioheadMesh.  DSR is described in https://tools.ietf.org/html/rfc4728
https://en.wikipedia.org/wiki/Dynamic_Source_Routing

broadcast solution:
Use naive flooding at first (FIXME - do some math for a 20 node, 3 hop mesh.  A single flood will require a max of 20 messages sent)
Then move to MPR later (http://www.olsr.org/docs/report_html/node28.html).  Use altitude and location as heursitics in selecting the MPR set

compare to db sync algorithm?

what about never flooding gps broadcasts.  instead only have them go one hop in the common case, but if any node X is looking at the position of Y on their gui, then send a unicast to Y asking for position update.  Y replies.

If Y were to die, at least the neighbor nodes of Y would have their last known position of Y.
