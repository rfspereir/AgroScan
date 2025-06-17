import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule, Router } from '@angular/router';
import { Auth, signOut } from '@angular/fire/auth';

@Component({
  selector: 'app-topbar',
  standalone: true,
  imports: [CommonModule, RouterModule],
  templateUrl: './topbar.html',
  styleUrls: ['./topbar.css']
})
export class Topbar {
  constructor(private auth: Auth, private router: Router) {}

  logout() {
    signOut(this.auth).then(() => {
      this.router.navigate(['/']);
    });
  }
}
