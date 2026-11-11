import { Button as ButtonPrimitive } from "@base-ui/react/button"
import { cva, type VariantProps } from "class-variance-authority"
import { cn } from "cn"

const buttonVariants = cva(
  "group/button inline-flex shrink-0 items-center justify-center gap-2 whitespace-nowrap rounded-full border border-primary bg-transparent font-light tracking-wide text-primary outline-none select-none transition-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background disabled:pointer-events-none disabled:opacity-40 aria-pressed:bg-primary aria-pressed:text-primary-foreground aria-expanded:bg-primary/15 data-[pressed]:bg-primary data-[pressed]:text-primary-foreground data-[selected]:bg-primary data-[selected]:text-primary-foreground [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4 [&_svg]:stroke-[1.25]",
  {
    variants: {
      variant: {
        default: "hover:bg-primary/15",
        solid: "bg-primary text-primary-foreground",
        outline: "hover:bg-primary/15",
        secondary: "hover:bg-primary/15",
        ghost: "border-transparent hover:border-primary",
        destructive: "hover:bg-primary/15",
        link: "rounded-none border-transparent underline underline-offset-4 hover:border-transparent",
      },
      size: {
        default: "h-8 px-3 text-sm",
        xs: "h-6 px-2 text-xs",
        sm: "h-7 px-2.5 text-xs",
        lg: "h-9 px-4 text-sm",
        icon: "size-8",
        "icon-xs": "size-6",
        "icon-sm": "size-7",
        "icon-lg": "size-9",
      },
    },
    defaultVariants: {
      variant: "default",
      size: "default",
    },
  }
)

function Button({
  className,
  variant = "default",
  size = "default",
  ...props
}: ButtonPrimitive.Props & VariantProps<typeof buttonVariants>) {
  return (
    <ButtonPrimitive
      data-slot="button"
      className={cn(buttonVariants({ variant, size, className }))}
      {...props}
    />
  )
}

export { Button, buttonVariants }
