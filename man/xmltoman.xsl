<?xml version="1.0" encoding="iso-8859-15"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns="http://www.w3.org/1999/xhtml">

<!-- 
  This file is part of shairport-sync.
  Copyright (c) Mike Brady 2014
  All rights reserved.
 
  Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software and associated documentation
  files (the "Software"), to deal in the Software without
  restriction, including without limitation the rights to use,
  copy, modify, merge, publish, distribute, sublicense, and/or
  sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:
 
  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.
 
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.
-->

<xsl:output method="xml" version="1.0" encoding="iso-8859-15" doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd" indent="yes"/>

<xsl:template match="/manpage">
  
    <html>

    <head>
      <title><xsl:value-of select="@name"/>(<xsl:value-of select="@section"/>)</title>
      <style type="text/css">
        body { color: black; background-color: white; } 
        a:link, a:visited { color: #900000; }       
        h1 { text-transform:uppercase; font-size: 18pt; } 
        p { margin-left:1cm; margin-right:1cm; } 
        .cmd { font-family:monospace; }
        .file { font-family:monospace; }
        .arg { font-family:monospace; font-style: italic; }
        .opt { font-family:monospace; font-weight: bold;  }
        .manref { font-family:monospace; }
        .option .optdesc { margin-left:2cm; }
      </style>
    </head>
    <body>
      <h1>Name</h1>
      <p><xsl:value-of select="@name"/>
        <xsl:if test="string-length(@desc) &gt; 0"> - <xsl:value-of select="@desc"/></xsl:if>
      </p>
      <xsl:apply-templates />
    </body>
  </html>
</xsl:template>

<xsl:template match="p">
 <p>
  <xsl:apply-templates/>
 </p>
</xsl:template>

<xsl:template match="cmd">
 <p class="cmd">
  <xsl:apply-templates/>
 </p>
</xsl:template>

<xsl:template match="arg">
  <span class="arg"><xsl:apply-templates/></span>
</xsl:template>

<xsl:template match="opt">
  <span class="opt"><xsl:apply-templates/></span>
</xsl:template>

<xsl:template match="file">
  <span class="file"><xsl:apply-templates/></span>
</xsl:template>

<xsl:template match="optdesc">
  <div class="optdesc">
    <xsl:apply-templates/>
  </div>
</xsl:template>

<xsl:template match="synopsis">
  <h1>Synopsis</h1>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="seealso">
  <h1>Synopsis</h1>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="description">
  <h1>Description</h1>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="options">
  <h1>Options</h1>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="section">
  <h1><xsl:value-of select="@name"/></h1>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="option">
  <div class="option"><xsl:apply-templates/></div>
</xsl:template>

<xsl:template match="manref">
  <xsl:choose>
    <xsl:when test="string-length(@href) &gt; 0">
    <a class="manref"><xsl:attribute name="href"><xsl:value-of select="@href"/></xsl:attribute><xsl:value-of select="@name"/>(<xsl:value-of select="@section"/>)</a>
    </xsl:when>
    <xsl:otherwise>
    <span class="manref"><xsl:value-of select="@name"/>(<xsl:value-of select="@section"/>)</span>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="url">
  <a class="url"><xsl:attribute name="href"><xsl:value-of select="@href"/></xsl:attribute><xsl:value-of select="@href"/></a>
</xsl:template>

</xsl:stylesheet>
