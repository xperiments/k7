import { mergeProps } from "@base-ui/react/merge-props"
import { useRender } from "@base-ui/react/use-render"
import { cva, type VariantProps } from "class-variance-authority"

import { cn } from "cn"

const badgeVariants = cva(
  [
    "relative inline-flex shrink-0 items-center justify-center w-fit border border-foreground/40 bg-transparent text-foreground whitespace-nowrap outline-none",
    "focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-1 focus-visible:ring-offset-background disabled:pointer-events-none disabled:opacity-40",
    "[&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*=size-])]:size-3 [&_svg]:stroke-[1.25]",
  ],
  {
    variants: {
      variant: {
        default: "bg-primary text-primary-foreground uppercase font-medium",
        outline: "bg-transparent text-foreground",
        secondary: "bg-transparent text-foreground",
        info: "bg-transparent text-foreground",
        success: "bg-transparent text-foreground",
        warning: "bg-transparent text-foreground",
        destructive: "bg-transparent text-foreground",
        focus: "bg-transparent text-foreground",
        invert: "bg-transparent text-foreground",
        "primary-light": "bg-transparent text-foreground",
        "warning-light": "bg-transparent text-foreground",
        "success-light": "bg-transparent text-foreground",
        "info-light": "bg-transparent text-foreground",
        "destructive-light": "bg-transparent text-foreground",
        "invert-light": "bg-transparent text-foreground",
        "focus-light": "bg-transparent text-foreground",
        "primary-outline": "bg-transparent text-foreground",
        "warning-outline": "bg-transparent text-foreground",
        "success-outline": "bg-transparent text-foreground",
        "info-outline": "bg-transparent text-foreground",
        "destructive-outline": "bg-transparent text-foreground",
        "invert-outline": "bg-transparent text-foreground",
        "focus-outline": "bg-transparent text-foreground",
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