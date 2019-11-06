---
title: SciDown Manifesto
author: Martino Ferrari
affiliation: University of Geneva
email: manda.mgf@gmail.com
keywords: SciDown, MarkDown, LaTeX, Publiscing
numbering: false
---

# Motivation

Honestly *LaTeX* is great, the quality of the output produced is outstanding, however its syntax is both dated and lengthy. 

Moreover due to the compilation time of the documents there are no (at my knowledge) editor with (real) real-time rendering capability, making the creation of the document more  unpleasant that it could (and should) be.

Finally today publishing for the *Web* is becoming more and more important and be able to produce documents as both as *pdf* and *html* is a big plus.

*MarkDown* is now becoming more and more popular, as *LaTeX* is a plain text language where the users do not have to think about formatting or styling. Moreover the language is very simple, intuitive and short (e.g. `# section tilte` against `\section{section title}`). However many features are missing to make such language ready for writing scientific publications, for example floating elements such as figures, tables and listings with or without captions.

The goal of ***SciDown*** is to complete *MarkDown* with some of this missing features, to be easily integrable in editors and views and compile in both *HTML* and *LaTeX*.

# Status

As today ***SciDown*** (and its *C* implementation) is already very capable, it generates both *HTML* and *LaTeX* (needs improvements) and adds many features to *MarkDown*:

 * Header (`YAML`): title, authors, keywords and more
 * Table of Contents
 * Abstract section (with optional keywords)
 * Floating elements (figures, tables, listings, equations) with labels and captions
 * Internal label references
 * Including external files, both as source or as references only
 * 2D charts as *SVG* and *PGFPLOTS* generation using [charter](https://github.com/mandarancio/charter)
 * Easy `CSS` styling
 
# Future Plans

For the future is planned to improve both stability of the parser and output quality for *HTML* and particularly for *LaTeX*. 

Improve the plotting capabilities and customization, configure the image size and alignments (e.g. sub-plots and multiple images per figure). 

Finally would be great to create a new simpler styling language to be used for both *HTML* and *LaTeX* output, or (maybe even better) to compile the `CSS` in a `LaTeX` understandable format.