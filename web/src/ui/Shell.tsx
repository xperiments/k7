import { useState } from 'react';
import {
  AudioLines,
  Disc3,
  ListMusic,
  Play,
  RefreshCw,
  ScanLine,
  Settings as SettingsIcon,
  Sparkles,
  Waves,
} from 'lucide-react';
import { cn } from 'cn';

import {
  Breadcrumb,
  BreadcrumbItem,
  BreadcrumbList,
  BreadcrumbPage,
  BreadcrumbSeparator,
} from '@/components/ui/breadcrumb';
import { Button } from '@/components/ui/button';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import {
  Sidebar,
  SidebarContent,
  SidebarFooter,
  SidebarGroup,
  SidebarGroupContent,
  SidebarGroupLabel,
  SidebarHeader,
  SidebarInset,
  SidebarMenu,
  SidebarMenuBadge,
  SidebarMenuButton,
  SidebarMenuItem,
  SidebarProvider,
  SidebarTrigger,
} from '@/components/ui/sidebar';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { useDrafts } from '../playlists/drafts';
import { navigate, routeHref, type Route } from './router';

/**
 * The shell: `c-sidebar-1`'s structure with this device's data in it.
 *
 * The template's three data blocks become: the device (where the workspace switcher
 * was), four nav groups with count badges, and a footer that links Settings and names
 * the licence. Its "Projects" group and account menu are gone -- there are no projects
 * and no accounts, and inventing them to keep the shape would be a lie in the UI.
 */

interface NavItem {
  href: string;
  label: string;
  icon: React.ReactNode;
  badge?: string;
  active: (route: Route) => boolean;
}

interface NavGroup {
  id: string;
  label: string;
  items: NavItem[];
}

const isProcess = (route: Route) => route.name === 'process';
const isScan = (route: Route) => route.name === 'scan';
const isPlaylists = (route: Route) => route.name === 'playlists' || route.name === 'playlist';
const isAsk = (route: Route) => route.name === 'ask';
const isMedia = (route: Route) =>
  route.name === 'media' || route.name === 'album' || route.name === 'artist' || route.name === 'video';
const isPlayer = (route: Route) => route.name === 'player';

/** The page's name inside its area, for the breadcrumb. */
function pageName(route: Route): string {
  switch (route.name) {
    case 'process':
      return 'Process';
    case 'scan':
      return 'Scan';
    case 'playlists':
      return 'Playlists';
    case 'playlist':
      return 'Editor';
    case 'ask':
      return 'Ask';
    case 'media':
      return 'Albums & artists';
    case 'album':
      return route.album;
    case 'artist':
      return route.artist;
    case 'video':
      return 'Video';
    case 'player':
      return 'Now playing';
    case 'settings':
      return 'Settings';
    default:
      return 'Not found';
  }
}

function areaName(route: Route): string | null {
  if (isProcess(route) || isScan(route)) return 'Card';
  if (isPlaylists(route) || isAsk(route)) return 'Playlists';
  if (isMedia(route)) return 'Media';
  if (isPlayer(route)) return 'Player';
  return null;
}

function DeviceMenu({ card, tracks, reachable }: { card: boolean; tracks: number; reachable: boolean }) {
  const [copied, setCopied] = useState(false);
  const stream = `http://${location.hostname}/stream`;

  const copyMcp = async () => {
    try {
      await navigator.clipboard.writeText(`http://${location.hostname}/mcp`);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 1500);
    } catch {
      /* a page without the clipboard permission simply does not copy */
    }
  };

  return (
    <SidebarMenu>
      <SidebarMenuItem>
        <DropdownMenu>
          <DropdownMenuTrigger
            render={<SidebarMenuButton size="lg" className="aria-expanded:bg-sidebar-accent aria-expanded:text-sidebar-accent-foreground" />}
          >
            <span className="flex size-8 shrink-0 items-center justify-center rounded-lg bg-sidebar-primary text-sidebar-primary-foreground">
              <AudioLines className="size-4" aria-hidden="true" />
            </span>
            <span className="grid min-w-0 flex-1 text-left leading-tight">
              <span className="truncate text-sm font-medium">xtape</span>
              <span className="truncate text-xs text-sidebar-foreground/70">
                {!reachable
                  ? 'device unreachable'
                  : card
                    ? `${tracks} tracks in the player`
                    : 'no card in the player'}
              </span>
            </span>
          </DropdownMenuTrigger>
          <DropdownMenuContent side="bottom" align="start" className="w-64">
            <DropdownMenuLabel>The device</DropdownMenuLabel>
            <DropdownMenuItem
              render={<a href={stream} target="_blank" rel="noopener noreferrer" />}
            >
              <Waves aria-hidden="true" />
              Open the live stream
            </DropdownMenuItem>
            <DropdownMenuItem render={<a href="/library" target="_blank" rel="noopener noreferrer" />}>
              <Disc3 aria-hidden="true" />
              Open the library index
            </DropdownMenuItem>
            <DropdownMenuItem onClick={() => void copyMcp()}>
              <Sparkles aria-hidden="true" />
              {copied ? 'Copied the MCP endpoint' : 'Copy the MCP endpoint'}
            </DropdownMenuItem>
            <DropdownMenuSeparator />
            <DropdownMenuItem
              onClick={() => {
                void fetch('/api/rescan', { method: 'POST' }).catch(() => {});
              }}
            >
              <RefreshCw aria-hidden="true" />
              Rescan the player's library
            </DropdownMenuItem>
          </DropdownMenuContent>
        </DropdownMenu>
      </SidebarMenuItem>
    </SidebarMenu>
  );
}

function Nav({ groups, route }: { groups: NavGroup[]; route: Route }) {
  return (
    <>
      {groups.map((group) => (
        <SidebarGroup key={group.id}>
          <SidebarGroupLabel>{group.label}</SidebarGroupLabel>
          <SidebarGroupContent>
            <SidebarMenu>
              {group.items.map((item) => (
                <SidebarMenuItem key={item.href}>
                  <SidebarMenuButton
                    render={<a href={item.href} />}
                    isActive={item.active(route)}
                    tooltip={item.label}
                  >
                    {item.icon}
                    <span>{item.label}</span>
                  </SidebarMenuButton>
                  {item.badge ? <SidebarMenuBadge>{item.badge}</SidebarMenuBadge> : null}
                </SidebarMenuItem>
              ))}
            </SidebarMenu>
          </SidebarGroupContent>
        </SidebarGroup>
      ))}
    </>
  );
}

export function Shell({ route, children }: { route: Route; children: React.ReactNode }) {
  const { status, reachable } = useDeviceStatus();
  const drafts = useDrafts();
  const playing = status?.playing ?? null;
  const card = Boolean(status?.card);
  const tracks = status?.tracks ?? 0;

  const groups: NavGroup[] = [
    {
      id: 'card',
      label: 'Card',
      items: [
        {
          href: routeHref({ name: 'process' }),
          label: 'Process',
          icon: <ListMusic aria-hidden="true" />,
          badge: tracks > 0 ? String(tracks) : undefined,
          active: isProcess,
        },
        { href: routeHref({ name: 'scan' }), label: 'Scan', icon: <ScanLine aria-hidden="true" />, active: isScan },
      ],
    },
    {
      id: 'playlists',
      label: 'Playlists',
      items: [
        {
          href: routeHref({ name: 'playlists' }),
          label: 'Manage',
          icon: <ListMusic aria-hidden="true" />,
          badge: drafts.length > 0 ? String(drafts.length) : undefined,
          active: isPlaylists,
        },
        { href: routeHref({ name: 'ask' }), label: 'Ask', icon: <Sparkles aria-hidden="true" />, active: isAsk },
      ],
    },
    {
      id: 'media',
      label: 'Media',
      items: [
        {
          href: routeHref({ name: 'media' }),
          label: 'Albums & artists',
          icon: <Disc3 aria-hidden="true" />,
          active: isMedia,
        },
      ],
    },
    {
      id: 'player',
      label: 'Player',
      items: [
        {
          href: routeHref({ name: 'player' }),
          label: 'Now playing',
          icon: <Play aria-hidden="true" />,
          badge: playing?.title ? '●' : undefined,
          active: isPlayer,
        },
      ],
    },
  ];

  const area = areaName(route);

  return (
    <SidebarProvider className="h-dvh min-h-0 w-full overflow-hidden">
      <Sidebar variant="inset" collapsible="icon">
        <SidebarHeader>
          <DeviceMenu card={card} tracks={tracks} reachable={reachable} />
        </SidebarHeader>
        <SidebarContent role="navigation" aria-label="Areas">
          <Nav groups={groups} route={route} />
        </SidebarContent>
        <SidebarFooter>
          <SidebarMenu>
            <SidebarMenuItem>
              <SidebarMenuButton
                render={<a href={routeHref({ name: 'settings' })} />}
                isActive={route.name === 'settings'}
                tooltip="Settings"
              >
                <SettingsIcon aria-hidden="true" />
                <span>Settings</span>
              </SidebarMenuButton>
            </SidebarMenuItem>
          </SidebarMenu>
          <p className="px-2 pb-1 text-xs text-sidebar-foreground/60">
            Models CC BY-NC 4.0 · AGPL-3.0
          </p>
        </SidebarFooter>
      </Sidebar>

      <SidebarInset className="min-w-0 overflow-hidden">
        <header className="flex h-12 shrink-0 items-center gap-2 border-b px-4">
          <SidebarTrigger className="-ml-1" />
          <Breadcrumb className="min-w-0">
            <BreadcrumbList className="flex-nowrap">
              {area ? (
                <>
                  <BreadcrumbItem className="min-w-0">
                    <span className="truncate text-sm text-muted-foreground">{area}</span>
                  </BreadcrumbItem>
                  <BreadcrumbSeparator />
                </>
              ) : null}
              <BreadcrumbItem>
                <BreadcrumbPage className="truncate">{pageName(route)}</BreadcrumbPage>
              </BreadcrumbItem>
            </BreadcrumbList>
          </Breadcrumb>
          <div className="ml-auto flex items-center gap-2">
            {!reachable ? (
              <span className="text-xs text-muted-foreground">device unreachable</span>
            ) : null}
            <Button
              variant="outline"
              size="sm"
              nativeButton={false}
              render={
                <a href={`http://${location.hostname}/stream`} target="_blank" rel="noopener noreferrer" />
              }
            >
              Stream
            </Button>
          </div>
        </header>

        <div className={cn('min-h-0 flex-1 overflow-auto')}>{children}</div>
      </SidebarInset>
    </SidebarProvider>
  );
}

/** The page a route that the shell cannot render lands on. */
export function NotFound({ path }: { path: string }) {
  return (
    <div className="flex flex-col gap-3 p-6">
      <h1 className="text-xl font-semibold tracking-tight">No such place</h1>
      <p className="text-sm text-muted-foreground">
        <code className="bg-muted px-1 py-0.5 font-mono text-xs">#{path}</code> is not one of the
        front door's routes. Card, Playlists, Media and Player are.
      </p>
      <div>
        <Button size="sm" onClick={() => navigate(routeHref({ name: 'process' }))}>
          Go to Process
        </Button>
      </div>
    </div>
  );
}
