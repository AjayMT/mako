
index.html: README.adoc light.min.css
	asciidoctor -a stylesheet=light.min.css -b html5 -o index.html README.adoc
