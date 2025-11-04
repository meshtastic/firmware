import { NodeId } from "./api";

// Storage interfaces and types
export interface MailMessage {
  to: NodeId;
  from: NodeId;
  body: string;
  timestamp: number;
  id: number;
}

export interface NewsPost {
  from: NodeId;
  body: string;
  timestamp: number;
  id: number;
}

export interface BBSStorage {
  // User registry
  registerUser(nodeId: NodeId, username: string): boolean;
  getUserByNodeId(nodeId: NodeId): string | null;
  getUserByUsername(username: string): NodeId | null;
  isUsernameTaken(username: string): boolean;

  // Mail operations
  addMail(to: NodeId, from: NodeId, body: string): void;
  getPendingMail(nodeId: NodeId): MailMessage[];
  markMailAsRead(nodeId: NodeId, mailIds: number[]): void;

  // News operations
  addNews(from: NodeId, body: string): void;
  getUnreadNews(nodeId: NodeId): NewsPost[];
  markNewsAsRead(nodeId: NodeId, newsIds: number[]): void;
}

// In-memory implementation
export class InMemoryBBSStorage implements BBSStorage {
  private usernameToNodeId: { [key: string]: NodeId } = {};
  private nodeIdToUsername: { [key: number]: string } = {};
  private mailMessages: MailMessage[] = [];
  private newsMessages: NewsPost[] = [];
  private mailReadMarkers: { [key: number]: { [key: number]: boolean } } = {};
  private newsReadMarkers: { [key: number]: { [key: number]: boolean } } = {};
  private nextMailId: number = 1;
  private nextNewsId: number = 1;

  // User registry methods
  registerUser(nodeId: NodeId, username: string): boolean {
    const normalizedUsername = username.toLowerCase();

    if (this.isUsernameTaken(normalizedUsername)) {
      return false;
    }

    // Remove old username if exists
    const oldUsername = this.nodeIdToUsername[nodeId];
    if (oldUsername) {
      delete this.usernameToNodeId[oldUsername];
    }

    this.usernameToNodeId[normalizedUsername] = nodeId;
    this.nodeIdToUsername[nodeId] = normalizedUsername;
    return true;
  }

  getUserByNodeId(nodeId: NodeId): string | null {
    return this.nodeIdToUsername[nodeId] || null;
  }

  getUserByUsername(username: string): NodeId | null {
    const normalized = username.toLowerCase();
    return this.usernameToNodeId[normalized] !== undefined
      ? this.usernameToNodeId[normalized]
      : null;
  }

  isUsernameTaken(username: string): boolean {
    return this.usernameToNodeId[username.toLowerCase()] !== undefined;
  }

  // Mail operations
  addMail(to: NodeId, from: NodeId, body: string): void {
    this.mailMessages.push({
      to,
      from,
      body,
      timestamp: Date.now(),
      id: this.nextMailId++,
    });
  }

  getPendingMail(nodeId: NodeId): MailMessage[] {
    const readIds = this.mailReadMarkers[nodeId] || {};
    return this.mailMessages.filter(
      (msg) => msg.to === nodeId && !readIds[msg.id]
    );
  }

  markMailAsRead(nodeId: NodeId, mailIds: number[]): void {
    if (!this.mailReadMarkers[nodeId]) {
      this.mailReadMarkers[nodeId] = {};
    }
    const readSet = this.mailReadMarkers[nodeId];
    mailIds.forEach((id) => (readSet[id] = true));
  }

  // News operations
  addNews(from: NodeId, body: string): void {
    this.newsMessages.push({
      from,
      body,
      timestamp: Date.now(),
      id: this.nextNewsId++,
    });
  }

  getUnreadNews(nodeId: NodeId): NewsPost[] {
    const readIds = this.newsReadMarkers[nodeId] || {};
    return this.newsMessages.filter((msg) => !readIds[msg.id]);
  }

  markNewsAsRead(nodeId: NodeId, newsIds: number[]): void {
    if (!this.newsReadMarkers[nodeId]) {
      this.newsReadMarkers[nodeId] = {};
    }
    const readSet = this.newsReadMarkers[nodeId];
    newsIds.forEach((id) => (readSet[id] = true));
  }
}

// Command handlers
export class BBSCommandHandler {
  constructor(private storage: BBSStorage) {}

  handleCommand(from: NodeId, message: string): string {
    const trimmed = message.trim();
    console.log(`BBSCommandHandler: handleCommand: ${trimmed}`);

    // Parse command
    if (trimmed.startsWith("/register ")) {
      return this.handleRegister(from, trimmed.substring(10).trim());
    } else if (trimmed.startsWith("/mail ")) {
      return this.handleSendMail(from, trimmed.substring(6).trim());
    } else if (trimmed === "/mail") {
      return this.handleFetchMail(from);
    } else if (trimmed === "/whoami") {
      return this.handleWhoAmI(from);
    } else if (trimmed.startsWith("/news ")) {
      return this.handlePostNews(from, trimmed.substring(6).trim());
    } else if (trimmed === "/news") {
      return this.handleFetchNews(from);
    } else {
      return this.handleWelcome(from);
    }
  }

  private handleRegister(from: NodeId, username: string): string {
    if (!username || username.length === 0) {
      return "Usage: /register <username>";
    }

    // Validate username (alphanumeric and underscore only)
    if (!/^[a-zA-Z0-9_]+$/.test(username)) {
      return "Error: Username must be alphanumeric (with underscores)";
    }

    const success = this.storage.registerUser(from, username);
    if (success) {
      return `Welcome to uBBS, ${username}!`;
    } else {
      return `Error: Username "${username}" is already taken`;
    }
  }

  private handleSendMail(from: NodeId, args: string): string {
    // Parse: <username> <body>
    const spaceIndex = args.indexOf(" ");
    if (spaceIndex === -1) {
      return "Usage: /mail <username> <body>";
    }

    const username = args.substring(0, spaceIndex);
    const body = args.substring(spaceIndex + 1).trim();

    if (!body) {
      return "Error: Message body cannot be empty";
    }

    const recipientNodeId = this.storage.getUserByUsername(username);
    if (!recipientNodeId) {
      return `Error: User "${username}" not found`;
    }

    this.storage.addMail(recipientNodeId, from, body);
    return `Mail sent to ${username}`;
  }

  private handleFetchMail(from: NodeId): string {
    const mail = this.storage.getPendingMail(from);

    if (mail.length === 0) {
      return "No pending mail";
    }

    // Mark all as read
    this.storage.markMailAsRead(
      from,
      mail.map((m) => m.id)
    );

    // Format messages
    const messages = mail.map((msg, index) => {
      const fromUsername =
        this.storage.getUserByNodeId(msg.from) ||
        `node:${msg.from.toString(16)}`;
      const date = new Date(msg.timestamp).toISOString().split("T")[0];
      return `${index + 1}. From ${fromUsername} (${date}):\n   ${msg.body}`;
    });

    return `You have ${mail.length} message(s):\n\n${messages.join("\n\n")}`;
  }

  private handleWhoAmI(from: NodeId): string {
    const username = this.storage.getUserByNodeId(from);
    if (username) {
      return `You are: ${username}`;
    } else {
      return "Not registered. Use /register <username> to register";
    }
  }

  private handlePostNews(from: NodeId, body: string): string {
    if (!body || body.length === 0) {
      return "Usage: /news <body>";
    }

    const username = this.storage.getUserByNodeId(from);
    if (!username) {
      return "Error: You must register before posting news. Use /register <username>";
    }

    this.storage.addNews(from, body);
    return "News posted successfully";
  }

  private handleFetchNews(from: NodeId): string {
    const news = this.storage.getUnreadNews(from);

    if (news.length === 0) {
      return "No unread news";
    }

    // Mark all as read
    this.storage.markNewsAsRead(
      from,
      news.map((n) => n.id)
    );

    // Format news items
    const items = news.map((item, index) => {
      const fromUsername =
        this.storage.getUserByNodeId(item.from) ||
        `node:${item.from.toString(16)}`;
      const date = new Date(item.timestamp).toISOString().split("T")[0];
      return `${index + 1}. ${fromUsername} (${date}):\n   ${item.body}`;
    });

    return `Unread news (${news.length}):\n\n${items.join("\n\n")}`;
  }

  private handleWelcome(from: NodeId): string {
    const mailCount = this.storage.getPendingMail(from).length;
    const newsCount = this.storage.getUnreadNews(from).length;

    const username = this.storage.getUserByNodeId(from);
    const greeting = username
      ? `Welcome back, ${username}!`
      : "Welcome to uBBS!";

    const parts: string[] = [];
    if (mailCount > 0) {
      parts.push(`${mailCount} unread message${mailCount === 1 ? "" : "s"}`);
    }
    if (newsCount > 0) {
      parts.push(`${newsCount} unread news item${newsCount === 1 ? "" : "s"}`);
    }

    if (parts.length > 0) {
      return `${greeting}\nYou have ${parts.join(" and ")}.`;
    } else {
      return `${greeting}\nNo unread items.`;
    }
  }
}

// Initialize and export singleton
export const bbsStorage = new InMemoryBBSStorage();
export const bbsHandler = new BBSCommandHandler(bbsStorage);
