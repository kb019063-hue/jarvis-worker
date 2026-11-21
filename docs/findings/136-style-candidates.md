## 136. Saving a selector candidate must preserve the traversal cursor

The mixed-combinator matcher needs two references to a matching element: one in the candidate list, and one to continue walking. It transferred the traversal reference into the list instead of retaining it. Both the ancestor walk and the preceding-sibling walk now copy the shared reference when recording a candidate. Moving the completed list entries onto the work stack remains valid.

Verification: run the frozen browsing check documented in `tools/real_pages/README.md`. The five Git manuals that had no initial frame all render; once link hit testing is corrected as well, all five navigation tasks pass. This is not a general claim about selector coverage or the fidelity of the manual layout.
