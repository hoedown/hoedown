
# Chapter

Lorem ipsum
dolor sit amet, consectetuer adipiscing elit, sed diam nonummy nibh euismod tincidunt ut
laoreet dolore magna aliquam erat volutpat. Ut wisi enim ad minim veniam, quis nostrud exerci tation
laoreet dolore magna aliquam erat volutpat. Ut wisi enim ad minim veniam, quis nostrud exerci tation
ullamcorper suscipit lobortis nisl ut aliquip ex ea commodo consequat. Duis autem vel eum iriure dolor in hendrerit in
vulputate velit esse molestie consequat, vel illum dolore eu feugiat nulla facilisis at vero eros et
ullamcorper suscipit lobortis nisl ut aliquip ex ea commodo consequat. Duis autem vel eum iriure dolor in hendrerit in
vulputate velit esse molestie consequat, vel illum dolore eu feugiat nulla facilisis at vero eros et

@table(tab:simple)

| Col. 1 | Col.2 | Col. 3|
| :----- | :----:| -----:|
| A      | B     | C     |
| D      | E     | F     |
| G      | H     | I     |

@caption(This is a table \(with 3 columns\).)

@/

accumsan et iusto odio dignissim qui blandit praesent luptatum zzril delenit augue duis dolore te feugait nulla facilisi.
Nam liber tempor cum soluta nobis eleifend option congue nihil imperdiet doming id quod mazim placerat facer possim assum.
laoreet dolore magna aliquam erat volutpat. Ut wisi enim ad minim veniam, quis nostrud exerci tation
ullamcorper suscipit lobortis nisl ut aliquip ex ea commodo consequat. Duis autem vel eum iriure dolor in hendrerit in
vulputate velit esse molestie consequat, vel illum dolore eu feugiat nulla facilisis at vero eros et

## Section
Lorem ipsum
dolor sit amet, consectetuer adipiscing elit, sed diam nonummy nibh euismod tincidunt ut
laoreet dolore magna aliquam erat volutpat. Ut wisi enim ad minim veniam, quis nostrud exerci tation this is a reference to the figure (#fig:falcon) representing a falcon rocket.

@listing()
```c
int main(int argc, char** argv)
{
	printf("hello world!\n");
	return 0;
}
```
@caption(This is a simple code \\\\not a very interesting example....)
@/

ullamcorper suscipit lobortis nisl ut aliquip ex ea commodo consequat. Duis autem vel eum iriure dolor in hendrerit in
vulputate velit esse molestie consequat, vel illum dolore eu feugiat nulla facilisis at vero eros et
accumsan et iusto odio dignissim qui blandit praesent luptatum zzril delenit augue duis dolore te feugait nulla facilisi.
Nam liber tempor cum soluta nobis eleifend option congue nihil imperdiet doming id quod mazim placerat facer possim assum.
This is a link [google](www.google.com)


### Subsection
This is a reference [^1].

This is an inline formula $x+3 = 2^2$ while this one is a block formula

$$
x = \sum_{i=-10}^n e^{-|n|}
$$

The following instead is a numbered and labeled equation:

@equation(eq:1)
x = \sum_{i=-10}^n e^{-|n|}
@/

The reference to a floating element is done in this way: the equation (#eq:1) show absolutely nothing.
mcorper suscipit lobortis nisl ut aliquip ex ea commodo consequat. Duis autem vel eum iriure dolor in hendrerit in
vulputate velit esse molestie consequat, vel illum dolore eu feugiat nulla facilisis at vero eros et
accumsan et iusto odio dignissim qui blandit praesent luptatum zzril delenit augue duis dolore te feugait nulla facilis

### Another subsection

This a plot

@figure(fig:plot)
```charter
width: 400
height: 300
plot:
	x: range: 0 2 20
	y: math: exp(-(x^2))
	label: gauss
x-axis:
	label: frequency
y-axis:
	label: energy
```

@caption(This is a simple plot, with linear axis and **no** title.)
@/


## Second section

Lorem ipsum
dolor sit amet, consectetuer adipiscing elit, sed diam nonummy nibh euismod tincidunt ut
laoreet dolore magna aliquam erat volutpat. Ut wisi enim ad minim veniam, quis nostrud exerci tation
ullamcorper suscipit lobortis nisl ut aliquip ex ea commodo consequat. Duis autem vel eum iriure dolor in hendrerit in

@figure(fig:falcon)
![Falcon Heavy](Falcon_Heavy.jpg)
@caption(This is an image.)
@/

vulputate velit esse molestie consequat, vel illum dolore eu feugiat nulla facilisis at vero eros et
accumsan et iusto odio dignissim qui blandit praesent luptatum zzril delenit augue duis dolore te feugait nulla facilisi.
Nam liber tempor cum soluta nobis eleifend option congue nihil imperdiet doming id quod mazim placerat facer possim assum.



@bib(references.md)
