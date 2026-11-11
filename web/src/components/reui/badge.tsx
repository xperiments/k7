import { mergeProps } from "@base-ui/react/merge-props"
import { useRender } from "@base-ui/react/use-render"
import { cva, type VariantProps } from "class-variance-authority"

import { cn } from "cn"

const badgeVariants = cva(
  [
    "relative inline-flex shrink-0 items-center justify-center w-fit border bg-transparent text-foreground whitespace-nowrap outline-none",
    "focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-1 focus-visible:ring-offset-background disabled:pointer-events-none disabled:opacity-40",
    "[&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*=size-])]:size-3 [&_svg]:stroke-[1.25]",
  ],
  {
    variants: {
      variant: {
        default: "border-primary bg-transparent text-primary uppercase font-medium",
        outline: "border-foreground/40 bg-transparent text-foreground",
        secondary: "border-foreground/40 bg-transparent text-foreground",
        info: "border-foreground/40 bg-transparent text-foreground",
        success: "border-foreground/40 bg-transparent text-foreground",
        warning: "border-foreground/40 bg-transparent text-foreground",
        destructive: "border-foreground/40 bg-transparent text-foreground",
        focus: "border-foreground/40 bg-transparent text-foreground",
        invert: "border-foreground/40 bg-transparent text-foreground",
        "primary-light": "border-foreground/40 bg-transparent text-foreground",
        "warning-light": "border-foreground/40 bg-transparent text-foreground",
        "success-light": "border-foreground/40 bg-transparent text-foreground",
        "info-light": "border-foreground/40 bg-transparent text-foreground",
        "destructive-light": "border-foreground/40 bg-transparent text-foreground",
        "invert-light": "border-foreground/40 bg-transparent text-foreground",
        "focus-light": "border-foreground/40 bg-transparent text-foreground",
        "primary-outline": "border-foreground/40 bg-transparent text-foreground",
        "warning-outline": "border-foreground/40 bg-transparent text-foreground",
        "success-outline": "border-foreground/40 bg-transparent text-foreground",
        "info-outline": "border-foreground/40 bg-transparent text-foreground",
        "destructive-outline": "border-foreground/40 bg-transparent text-foreground",
        "invert-outline": "border-foreground/40 bg-transparent text-foreground",
        "focus-outline": "border-foreground/40 bg-transparent text-foreground",
      },
      size: {
        xs: "h-4.5 min-w-4.5 px-1.5 text-xs leading-none gap-1",
        sm: "h-4.5 min-w-4.5 px-1.5 text-xs leading-none gap-1",
        default: "h-5 min-w-5 px-2 text-xs leading-none gap-1",
        lg: "h-5.5 min-w-5.5 px-2 text-xs leading-none gap-1",
        xl: "h-6 min-w-6 px-2.5 text-sm leading-none gap-1.5",
      },
      /** `default`: active style radius. `full`: pill radius. */
      radius: {
        default: "rounded-full",
        full: "rounded-full",
      },
    },
    defaultVariants: {
      variant: "default",
      size: "default",
      radius: "default",
    },
  }
)

interface BadgeProps extends useRender.ComponentProps<"span"> {
  variant?: VariantProps<typeof badgeVariants>["variant"]
  size?: VariantProps<typeof badgeVariants>["size"]
  radius?: VariantProps<typeof badgeVariants>["radius"]
}

function Badge({
  className,
  variant,
  size,
  radius,
  render,
  ...props
}: BadgeProps) {
  const defaultProps = {
    "data-slot": "badge",
    className: cn(badgeVariants({ variant, size, radius, className })),
  }

  return useRender({
    defaultTagName: "span",
    render,
    props: mergeProps<"span">(defaultProps, props),
  })
}

export { Badge, badgeVariants, type BadgeProps }